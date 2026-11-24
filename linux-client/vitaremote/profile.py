"""Perfis de mapeamento (arquivos TOML).

Um perfil diz o que cada entrada do Vita faz no Linux. Formato de um "alvo":

    "pad:A"                     botão do gamepad virtual (A B X Y LB RB LT RT BACK START
                                GUIDE LS RS HAT_UP HAT_DOWN HAT_LEFT HAT_RIGHT ou BTN_*)
    "key:ENTER"                 tecla do teclado virtual (nomes do kernel, com ou sem KEY_)
    "key:LEFTCTRL+C"            combinação: todas as teclas ficam pressionadas juntas
    "mouse:LEFT"                botão do mouse virtual (LEFT RIGHT MIDDLE SIDE EXTRA)
    "none"                      não faz nada

Veja os perfis prontos em vitaremote/profiles/.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from evdev import ecodes

from .protocol_v1 import ALL_BUTTONS

BUILTIN_DIR = Path(__file__).parent / "profiles"

PAD_ALIASES = {
    "A": "BTN_SOUTH",
    "B": "BTN_EAST",
    "X": "BTN_X",
    "Y": "BTN_Y",
    "LB": "BTN_TL",
    "RB": "BTN_TR",
    "BACK": "BTN_SELECT",
    "START": "BTN_START",
    "GUIDE": "BTN_MODE",
    "LS": "BTN_THUMBL",
    "RS": "BTN_THUMBR",
}
PAD_BUTTON_CODES = frozenset(ecodes.ecodes[n] for n in PAD_ALIASES.values())
# Alvos do gamepad que viram eixos, não botões.
PAD_SPECIAL = frozenset({"HAT_UP", "HAT_DOWN", "HAT_LEFT", "HAT_RIGHT", "LT", "RT"})
MOUSE_BUTTON_CODES = frozenset(
    ecodes.ecodes[n] for n in ("BTN_LEFT", "BTN_RIGHT", "BTN_MIDDLE", "BTN_SIDE", "BTN_EXTRA")
)

STICK_KINDS = ("pad", "mouse", "scroll", "keys", "none")


class ProfileError(ValueError):
    pass


@dataclass(frozen=True)
class Target:
    device: str  # "pad" | "key" | "mouse"
    codes: tuple  # ints (EV_KEY) ou nomes especiais do pad ("HAT_UP", "LT"...)


@dataclass(frozen=True)
class StickMode:
    kind: str  # um de STICK_KINDS
    pad_side: str = ""  # "left" | "right" quando kind == "pad"
    keys: tuple[Target | None, ...] = ()  # cima, esquerda, baixo, direita quando kind == "keys"


@dataclass
class Profile:
    name: str
    buttons: dict[str, Target | None] = field(default_factory=dict)
    left_stick: StickMode = StickMode("none")
    right_stick: StickMode = StickMode("none")
    deadzone: float = 0.12
    touch_front: str = "none"  # "touchpad" | "none"
    touch_sensitivity: float = 0.6
    tap: Target | None = None
    tap_ms: int = 180
    tap_travel: int = 40
    two_finger_scroll: bool = False  # arrastar com 2 dedos = rolar (só protocolo v2)
    two_finger_tap: Target | None = None  # toque rápido com 2 dedos (só protocolo v2)
    touch_scroll_sensitivity: float = 0.03  # "cliques" de rolagem por unidade de toque
    stick_mouse_speed: float = 1400.0  # pixels/s com o analógico no máximo
    scroll_speed: float = 12.0  # "cliques" de rolagem/s no máximo

    def needed_devices(self) -> set[str]:
        devices: set[str] = set()
        targets = [t for t in self.buttons.values() if t]
        targets += [t for t in (self.tap, self.two_finger_tap) if t]
        for stick in (self.left_stick, self.right_stick):
            if stick.kind == "pad":
                devices.add("pad")
            elif stick.kind in ("mouse", "scroll"):
                devices.add("mouse")
            elif stick.kind == "keys":
                targets += [t for t in stick.keys if t]
        if self.touch_front == "touchpad" or self.two_finger_scroll:
            devices.add("mouse")
        devices.update(t.device for t in targets)
        return devices


def _key_code(name: str, where: str) -> int:
    full = name if name.startswith("KEY_") else f"KEY_{name}"
    code = ecodes.ecodes.get(full)
    if code is None:
        raise ProfileError(f"{where}: tecla desconhecida '{name}' (use nomes como ENTER, A, LEFTCTRL)")
    return code


def _mouse_code(name: str, where: str) -> int:
    full = name if name.startswith("BTN_") else f"BTN_{name}"
    code = ecodes.ecodes.get(full)
    if code not in MOUSE_BUTTON_CODES:
        raise ProfileError(f"{where}: botão de mouse inválido '{name}' (use LEFT, RIGHT, MIDDLE, SIDE, EXTRA)")
    return code


def _pad_code(name: str, where: str) -> int | str:
    if name in PAD_SPECIAL:
        return name
    code = ecodes.ecodes.get(PAD_ALIASES.get(name, name))
    if code not in PAD_BUTTON_CODES:
        valid = ", ".join([*PAD_ALIASES, *sorted(PAD_SPECIAL)])
        raise ProfileError(f"{where}: botão de gamepad inválido '{name}' (válidos: {valid})")
    return code


_RESOLVERS = {"key": _key_code, "mouse": _mouse_code, "pad": _pad_code}


def parse_target(value: object, where: str) -> Target | None:
    if not isinstance(value, str):
        raise ProfileError(f"{where}: esperado texto como \"key:ENTER\", veio {value!r}")
    text = value.strip()
    if text.lower() == "none":
        return None
    device, sep, rest = text.partition(":")
    device = device.lower()
    if not sep or device not in _RESOLVERS or not rest:
        raise ProfileError(f"{where}: alvo inválido '{value}' (formato: pad:A, key:ENTER, mouse:LEFT ou none)")
    names = [n.strip().upper() for n in rest.split("+")]
    codes = tuple(_RESOLVERS[device](n, where) for n in names)
    return Target(device, codes)


def _parse_stick(value: object, where: str) -> StickMode:
    if not isinstance(value, str):
        raise ProfileError(f"{where}: esperado texto, veio {value!r}")
    kind, _, rest = value.strip().partition(":")
    kind = kind.lower()
    if kind == "pad":
        side = rest.lower()
        if side not in ("left", "right"):
            raise ProfileError(f"{where}: use \"pad:left\" ou \"pad:right\"")
        return StickMode("pad", pad_side=side)
    if kind == "keys":
        parts = [p.strip() for p in rest.split(",")]
        if len(parts) != 4:
            raise ProfileError(f"{where}: use \"keys:CIMA,ESQUERDA,BAIXO,DIREITA\", ex. \"keys:W,A,S,D\"")
        return StickMode("keys", keys=tuple(parse_target(f"key:{p}", where) for p in parts))
    if kind in ("mouse", "scroll", "none") and not rest:
        return StickMode(kind)
    raise ProfileError(f"{where}: modo inválido '{value}' (válidos: pad:left, pad:right, mouse, scroll, keys:..., none)")


def _number(table: dict, key: str, default: float, where: str, lo: float, hi: float) -> float:
    value = table.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not lo <= value <= hi:
        raise ProfileError(f"{where}.{key}: esperado número entre {lo} e {hi}, veio {value!r}")
    return value


def profile_from_dict(data: dict, default_name: str = "perfil") -> Profile:
    known = {"name", "buttons", "sticks", "touch", "cursor"}
    unknown = set(data) - known
    if unknown:
        raise ProfileError(f"seções desconhecidas: {', '.join(sorted(unknown))}")

    profile = Profile(name=str(data.get("name", default_name)))

    buttons = data.get("buttons", {})
    for name, value in buttons.items():
        if name not in ALL_BUTTONS:
            raise ProfileError(f"buttons.{name}: botão do Vita desconhecido (válidos: {', '.join(ALL_BUTTONS)})")
        profile.buttons[name] = parse_target(value, f"buttons.{name}")

    sticks = data.get("sticks", {})
    profile.left_stick = _parse_stick(sticks.get("left", "none"), "sticks.left")
    profile.right_stick = _parse_stick(sticks.get("right", "none"), "sticks.right")
    profile.deadzone = _number(sticks, "deadzone", profile.deadzone, "sticks", 0.0, 0.9)

    touch = data.get("touch", {})
    front = touch.get("front", "none")
    if front not in ("touchpad", "none"):
        raise ProfileError("touch.front: use \"touchpad\" ou \"none\"")
    profile.touch_front = front
    profile.touch_sensitivity = _number(touch, "sensitivity", profile.touch_sensitivity, "touch", 0.05, 10)
    profile.tap = parse_target(touch.get("tap", "none"), "touch.tap")
    profile.tap_ms = int(_number(touch, "tap_ms", profile.tap_ms, "touch", 50, 1000))
    profile.tap_travel = int(_number(touch, "tap_travel", profile.tap_travel, "touch", 0, 500))
    two_scroll = touch.get("two_finger_scroll", False)
    if not isinstance(two_scroll, bool):
        raise ProfileError("touch.two_finger_scroll: use true ou false")
    profile.two_finger_scroll = two_scroll
    profile.two_finger_tap = parse_target(touch.get("two_finger_tap", "none"), "touch.two_finger_tap")
    profile.touch_scroll_sensitivity = _number(
        touch, "scroll_sensitivity", profile.touch_scroll_sensitivity, "touch", 0.001, 1)

    cursor = data.get("cursor", {})
    profile.stick_mouse_speed = _number(cursor, "stick_speed", profile.stick_mouse_speed, "cursor", 50, 20000)
    profile.scroll_speed = _number(cursor, "scroll_speed", profile.scroll_speed, "cursor", 1, 200)
    return profile


MODES = ("gamepad", "mouse", "keyboard")


@dataclass
class ProfileSet:
    """Um perfil por modo do Vita (protocolo v2). Perfis antigos valem para todos os modos."""

    name: str
    modes: dict[str, Profile]

    def for_mode(self, mode: str) -> Profile:
        return self.modes[mode]

    def needed_devices(self) -> set[str]:
        devices: set[str] = set()
        for profile in self.modes.values():
            devices |= profile.needed_devices()
        return devices


def profile_set_from_dict(data: dict, default_name: str = "perfil") -> ProfileSet:
    name = str(data.get("name", default_name))
    if not any(m in data for m in MODES):
        single = profile_from_dict(data, default_name)
        return ProfileSet(single.name, {m: single for m in MODES})
    unknown = set(data) - {"name", *MODES}
    if unknown:
        raise ProfileError(f"seções desconhecidas: {', '.join(sorted(unknown))} (use [gamepad], [mouse], [keyboard])")
    modes = {}
    for mode in MODES:
        try:
            modes[mode] = profile_from_dict(data.get(mode, {}), f"{name} ({mode})")
        except ProfileError as exc:
            raise ProfileError(f"[{mode}] {exc}") from exc
    return ProfileSet(name, modes)


def builtin_profiles() -> list[str]:
    return sorted(p.stem for p in BUILTIN_DIR.glob("*.toml"))


def _read(name_or_path: str) -> tuple[Path, dict]:
    path = Path(name_or_path)
    if not path.suffix:
        path = BUILTIN_DIR / f"{name_or_path}.toml"
    if not path.is_file():
        raise ProfileError(
            f"perfil '{name_or_path}' não encontrado. Prontos: {', '.join(builtin_profiles())}, "
            "ou passe o caminho de um arquivo .toml"
        )
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as exc:
        raise ProfileError(f"{path}: TOML inválido: {exc}") from exc
    return path, data


def load_profile(name_or_path: str) -> Profile:
    """Perfil de modo único (protocolo v1). Num perfil por modos, usa o modo gamepad."""
    return load_profile_set(name_or_path).for_mode("gamepad")


def load_profile_set(name_or_path: str) -> ProfileSet:
    path, data = _read(name_or_path)
    try:
        return profile_set_from_dict(data, default_name=path.stem)
    except ProfileError as exc:
        raise ProfileError(f"{path}: {exc}") from exc
