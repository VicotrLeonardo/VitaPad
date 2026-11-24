"""Traduz o estado do Vita em eventos de entrada, segundo um Profile.

O Mapper não sabe nada de rede nem de uinput: recebe um VitaState por quadro e
chama métodos de um Output. Isso deixa a lógica testável sem /dev/uinput.

Estratégia: a cada quadro calculamos o estado *desejado* de todos os botões
(gamepad, teclado, mouse) e emitimos só a diferença em relação ao quadro anterior.
Assim, duas entradas mapeadas na mesma tecla não "brigam", e soltar tudo numa
desconexão é trivial.
"""

from __future__ import annotations

import math
from typing import Protocol

from .profile import Profile, StickMode, Target
from .protocol_v1 import ALL_BUTTONS, VitaState

ButtonKey = tuple[str, object]  # (dispositivo, código)

MAX_DT = 0.1  # evita "saltos" do cursor depois de uma travada de rede
STICK_MOUSE_CURVE = 2.0  # >1 = mais precisão perto do centro


class Output(Protocol):
    def set_button(self, device: str, code: int, pressed: bool) -> None: ...
    def set_axis(self, name: str, value: int) -> None: ...
    def move_mouse(self, dx: int, dy: int) -> None: ...
    def scroll(self, vertical: int, horizontal: int) -> None: ...
    def sync(self) -> None: ...


def normalize_stick(raw_x: int, raw_y: int, deadzone: float) -> tuple[float, float]:
    """0..255 -> -1..1 com zona morta radial (círculo, não quadrado)."""
    x = max(-1.0, min(1.0, (raw_x - 128) / 127.0))
    y = max(-1.0, min(1.0, (raw_y - 128) / 127.0))
    mag = math.hypot(x, y)
    if mag <= deadzone:
        return 0.0, 0.0
    scaled = min(1.0, (mag - deadzone) / (1.0 - deadzone))
    return x / mag * scaled, y / mag * scaled


class Mapper:
    def __init__(self, profile: Profile, output: Output) -> None:
        self.profile = profile
        self.out = output
        self._pressed: dict[ButtonKey, bool] = {}
        self._axes: dict[str, int] = {}
        self._mouse_rem = [0.0, 0.0]
        self._scroll_rem = [0.0, 0.0]
        self._touching = False
        self._last_touch = (0, 0)
        self._touch_origin = (0, 0)
        self._touch_travel = 0.0
        self._touch_time = 0.0
        self._pulses: list[Target] = []

    # ------------------------------------------------------------------ quadro

    def update(self, state: VitaState, dt: float) -> None:
        dt = max(0.0, min(dt, MAX_DT))
        desired: dict[ButtonKey, bool] = {}

        def want(target: Target | None, on: bool) -> None:
            if target is None:
                return
            for code in target.codes:
                key = (target.device, code)
                desired[key] = desired.get(key, False) or on

        for name in ALL_BUTTONS:
            want(self.profile.buttons.get(name), state.pressed(name))

        mouse = [0.0, 0.0]
        wheel = [0.0, 0.0]
        sticks = (
            (self.profile.left_stick, state.lx, state.ly),
            (self.profile.right_stick, state.rx, state.ry),
        )
        for mode, raw_x, raw_y in sticks:
            self._stick(mode, raw_x, raw_y, dt, want, mouse, wheel)

        self._touch(state, dt, mouse)

        # Toques rápidos geram um clique de um quadro: pressiona agora, solta no próximo.
        for target in self._pulses:
            want(target, True)
        self._pulses.clear()

        self._emit_pad_specials(desired)
        self._emit_buttons(desired)
        self._emit_mouse(mouse, wheel)
        self.out.sync()

    def release_all(self) -> None:
        """Solta tudo e centraliza eixos. Chamado ao desconectar ou sair."""
        for (device, code), on in self._pressed.items():
            if on:
                self.out.set_button(device, code, False)
        self._pressed.clear()
        for name, value in self._axes.items():
            if value != 0:
                self.out.set_axis(name, 0)
        self._axes = {name: 0 for name in self._axes}
        self._touching = False
        self._pulses.clear()
        self._mouse_rem = [0.0, 0.0]
        self._scroll_rem = [0.0, 0.0]
        self.out.sync()

    # ----------------------------------------------------------------- entradas

    def _stick(self, mode: StickMode, raw_x, raw_y, dt, want, mouse, wheel) -> None:
        if mode.kind == "none":
            return
        x, y = normalize_stick(raw_x, raw_y, self.profile.deadzone)
        if mode.kind == "pad":
            ax, ay = ("ABS_X", "ABS_Y") if mode.pad_side == "left" else ("ABS_RX", "ABS_RY")
            self._set_axis(ax, round(x * 32767))
            self._set_axis(ay, round(y * 32767))
        elif mode.kind == "keys":
            up, left, down, right = mode.keys
            want(up, y < -0.5)
            want(left, x < -0.5)
            want(down, y > 0.5)
            want(right, x > 0.5)
        elif mode.kind == "mouse":
            mag = math.hypot(x, y)
            if mag > 0:
                speed = self.profile.stick_mouse_speed * mag ** STICK_MOUSE_CURVE / mag
                mouse[0] += x * speed * dt
                mouse[1] += y * speed * dt
        elif mode.kind == "scroll":
            # Analógico para cima = rolar para cima (REL_WHEEL positivo).
            wheel[0] += -y * self.profile.scroll_speed * dt
            wheel[1] += x * self.profile.scroll_speed * dt

    def _touch(self, state: VitaState, dt: float, mouse: list[float]) -> None:
        pos = (state.touch_x, state.touch_y)
        if state.front_touch:
            if not self._touching:
                self._touching = True
                self._touch_origin = pos
                self._touch_travel = 0.0
                self._touch_time = 0.0
            else:
                self._touch_time += dt
                if self.profile.touch_front == "touchpad":
                    sens = self.profile.touch_sensitivity
                    mouse[0] += (pos[0] - self._last_touch[0]) * sens
                    mouse[1] += (pos[1] - self._last_touch[1]) * sens
            self._touch_travel = max(
                self._touch_travel,
                math.hypot(pos[0] - self._touch_origin[0], pos[1] - self._touch_origin[1]),
            )
            self._last_touch = pos
        elif self._touching:
            self._touching = False
            quick = self._touch_time * 1000 <= self.profile.tap_ms
            still = self._touch_travel <= self.profile.tap_travel
            if self.profile.tap and quick and still:
                self._pulses.append(self.profile.tap)

    # ------------------------------------------------------------------ saídas

    def _set_axis(self, name: str, value: int) -> None:
        if self._axes.get(name) != value:
            self._axes[name] = value
            self.out.set_axis(name, value)

    def _emit_pad_specials(self, desired: dict[ButtonKey, bool]) -> None:
        """D-pad e gatilhos do gamepad são eixos (HAT0, Z/RZ), não botões."""
        special = {code: desired.pop(("pad", code), None) for code in
                   ("HAT_UP", "HAT_DOWN", "HAT_LEFT", "HAT_RIGHT", "LT", "RT")}
        if any(v is not None for v in (special["HAT_UP"], special["HAT_DOWN"],
                                       special["HAT_LEFT"], special["HAT_RIGHT"])):
            self._set_axis("ABS_HAT0X", int(bool(special["HAT_RIGHT"])) - int(bool(special["HAT_LEFT"])))
            self._set_axis("ABS_HAT0Y", int(bool(special["HAT_DOWN"])) - int(bool(special["HAT_UP"])))
        if special["LT"] is not None:
            self._set_axis("ABS_Z", 255 if special["LT"] else 0)
        if special["RT"] is not None:
            self._set_axis("ABS_RZ", 255 if special["RT"] else 0)

    def _emit_buttons(self, desired: dict[ButtonKey, bool]) -> None:
        # Primeiro solta, depois pressiona (na ordem do perfil: modificadores antes da tecla).
        for key, was in list(self._pressed.items()):
            if was and not desired.get(key, False):
                self.out.set_button(key[0], key[1], False)
                self._pressed[key] = False
        for key, on in desired.items():
            if on and not self._pressed.get(key, False):
                self.out.set_button(key[0], key[1], True)
                self._pressed[key] = True

    def _emit_mouse(self, mouse: list[float], wheel: list[float]) -> None:
        for i in (0, 1):
            self._mouse_rem[i] += mouse[i]
            self._scroll_rem[i] += wheel[i]
        dx, dy = int(self._mouse_rem[0]), int(self._mouse_rem[1])
        if dx or dy:
            self._mouse_rem[0] -= dx
            self._mouse_rem[1] -= dy
            self.out.move_mouse(dx, dy)
        sv, sh = int(self._scroll_rem[0]), int(self._scroll_rem[1])
        if sv or sh:
            self._scroll_rem[0] -= sv
            self._scroll_rem[1] -= sh
            self.out.scroll(sv, sh)
