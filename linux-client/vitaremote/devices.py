"""Saídas do Mapper: dispositivos uinput reais ou impressão no terminal (--dry-run)."""

from __future__ import annotations

from evdev import AbsInfo, UInput, UInputError, ecodes

from .profile import MOUSE_BUTTON_CODES, PAD_BUTTON_CODES

# IDs de um controle Xbox 360 com fio: faz Steam/SDL/jogos reconhecerem o layout
# automaticamente, sem configurar nada.
XBOX360_VENDOR = 0x045E
XBOX360_PRODUCT = 0x028E

_STICK = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
_TRIGGER = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
_HAT = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)

PAD_AXES = {
    "ABS_X": _STICK, "ABS_Y": _STICK, "ABS_RX": _STICK, "ABS_RY": _STICK,
    "ABS_Z": _TRIGGER, "ABS_RZ": _TRIGGER, "ABS_HAT0X": _HAT, "ABS_HAT0Y": _HAT,
}

# Teclado "completo": todas as teclas KEY_* comuns (códigos 1..255).
KEYBOARD_CODES = sorted(
    code for code, names in ecodes.KEY.items()
    if 0 < code < 256 and any(n.startswith("KEY_") for n in ([names] if isinstance(names, str) else names))
)


class UInputPermissionError(RuntimeError):
    pass


def _create(**kwargs) -> UInput:
    try:
        return UInput(**kwargs)
    except (PermissionError, UInputError, OSError) as exc:
        raise UInputPermissionError(
            f"não consegui criar o dispositivo virtual ({exc}).\n"
            "Verifique a permissão de escrita em /dev/uinput (veja a seção 'Permissões' do README)."
        ) from exc


class UInputOutput:
    def __init__(self, devices: set[str], name_prefix: str = "Vita Remote") -> None:
        self._dev: dict[str, UInput] = {}
        self._dirty: set[str] = set()
        try:
            if "pad" in devices:
                self._dev["pad"] = _create(
                    events={
                        ecodes.EV_KEY: sorted(PAD_BUTTON_CODES),
                        ecodes.EV_ABS: [(ecodes.ecodes[n], info) for n, info in PAD_AXES.items()],
                    },
                    name=f"{name_prefix} Gamepad",
                    vendor=XBOX360_VENDOR, product=XBOX360_PRODUCT, version=0x0110,
                    bustype=ecodes.BUS_USB,
                )
            if "key" in devices:
                self._dev["key"] = _create(
                    events={ecodes.EV_KEY: KEYBOARD_CODES},
                    name=f"{name_prefix} Keyboard", bustype=ecodes.BUS_VIRTUAL,
                )
            if "mouse" in devices:
                self._dev["mouse"] = _create(
                    events={
                        ecodes.EV_KEY: sorted(MOUSE_BUTTON_CODES),
                        ecodes.EV_REL: [ecodes.REL_X, ecodes.REL_Y, ecodes.REL_WHEEL, ecodes.REL_HWHEEL],
                    },
                    name=f"{name_prefix} Mouse", bustype=ecodes.BUS_VIRTUAL,
                )
        except Exception:
            self.close()
            raise

    @property
    def device_names(self) -> list[str]:
        return [d.name for d in self._dev.values()]

    def set_button(self, device: str, code: int, pressed: bool) -> None:
        self._dev[device].write(ecodes.EV_KEY, code, int(pressed))
        self._dirty.add(device)

    def set_axis(self, name: str, value: int) -> None:
        self._dev["pad"].write(ecodes.EV_ABS, ecodes.ecodes[name], value)
        self._dirty.add("pad")

    def move_mouse(self, dx: int, dy: int) -> None:
        dev = self._dev["mouse"]
        if dx:
            dev.write(ecodes.EV_REL, ecodes.REL_X, dx)
        if dy:
            dev.write(ecodes.EV_REL, ecodes.REL_Y, dy)
        self._dirty.add("mouse")

    def scroll(self, vertical: int, horizontal: int) -> None:
        dev = self._dev["mouse"]
        if vertical:
            dev.write(ecodes.EV_REL, ecodes.REL_WHEEL, vertical)
        if horizontal:
            dev.write(ecodes.EV_REL, ecodes.REL_HWHEEL, horizontal)
        self._dirty.add("mouse")

    def sync(self) -> None:
        for device in self._dirty:
            self._dev[device].syn()
        self._dirty.clear()

    def close(self) -> None:
        for dev in self._dev.values():
            dev.close()
        self._dev.clear()


def code_name(code: object) -> str:
    if isinstance(code, str):
        return code
    names = ecodes.KEY.get(code) or ecodes.BTN.get(code) or str(code)
    return names if isinstance(names, str) else names[0]


class PrintOutput:
    """--dry-run: mostra no terminal o que seria enviado, sem criar dispositivos."""

    def __init__(self) -> None:
        self._buf: list[str] = []

    def set_button(self, device: str, code: int, pressed: bool) -> None:
        self._buf.append(f"{device}:{code_name(code)} {'↓' if pressed else '↑'}")

    def set_axis(self, name: str, value: int) -> None:
        self._buf.append(f"pad:{name}={value}")

    def move_mouse(self, dx: int, dy: int) -> None:
        self._buf.append(f"mouse:move({dx:+d},{dy:+d})")

    def scroll(self, vertical: int, horizontal: int) -> None:
        self._buf.append(f"mouse:scroll({vertical:+d},{horizontal:+d})")

    def sync(self) -> None:
        if self._buf:
            print("  ".join(self._buf), flush=True)
            self._buf.clear()

    def close(self) -> None:
        pass
