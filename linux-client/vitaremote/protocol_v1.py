"""Protocolo do VitaPad original (v1.3, Rinnegatamante).

Como funciona:
  * O Vita abre um servidor TCP na porta 5000.
  * O PC conecta e, a cada leitura, envia 8 bytes ("request\\0").
  * O Vita responde com um PadPacket de 16 bytes (little-endian):

      offset  tipo    campo
      0       uint32  buttons   (bitmask SceCtrl)
      4       uint8   lx        analógico esquerdo X (0..255, 128 = centro)
      5       uint8   ly        analógico esquerdo Y (0 = cima)
      6       uint8   rx
      7       uint8   ry
      8       uint16  tx        toque frontal X (0..1919)
      10      uint16  ty        toque frontal Y (0..1087)
      12      uint8   click     flags (ver FLAG_*)
      13..15  -       padding do compilador
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

PORT = 5000
REQUEST = b"request\x00"

_PACKET = struct.Struct("<I4BHHB3x")
PACKET_SIZE = _PACKET.size  # 16

# Tamanho da área de toque frontal, em unidades do Vita.
TOUCH_W = 1920
TOUCH_H = 1088

# Flags do campo "click".
FLAG_FRONT_TOUCH = 0x01  # tem dedo na tela frontal
FLAG_REAR_LEFT = 0x08  # toque traseiro na metade esquerda
FLAG_REAR_RIGHT = 0x10  # toque traseiro na metade direita

# Bitmask SceCtrl (psp2/ctrl.h). L3/R3/L1/R1 só existem no PS TV / DS3/DS4.
BUTTONS: dict[str, int] = {
    "select": 0x0001,
    "l3": 0x0002,
    "r3": 0x0004,
    "start": 0x0008,
    "up": 0x0010,
    "right": 0x0020,
    "down": 0x0040,
    "left": 0x0080,
    "l": 0x0100,  # L do Vita (LTRIGGER)
    "r": 0x0200,  # R do Vita (RTRIGGER)
    "l1": 0x0400,
    "r1": 0x0800,
    "triangle": 0x1000,
    "circle": 0x2000,
    "cross": 0x4000,
    "square": 0x8000,
}

# "Botões" virtuais que não vêm do bitmask, mas das flags de toque traseiro.
VIRTUAL_BUTTONS = ("rear_left", "rear_right")
ALL_BUTTONS = tuple(BUTTONS) + VIRTUAL_BUTTONS


@dataclass(frozen=True)
class VitaState:
    buttons: int = 0
    lx: int = 128
    ly: int = 128
    rx: int = 128
    ry: int = 128
    touch_x: int = 0
    touch_y: int = 0
    front_touch: bool = False
    rear_left: bool = False
    rear_right: bool = False

    def pressed(self, name: str) -> bool:
        if name == "rear_left":
            return self.rear_left
        if name == "rear_right":
            return self.rear_right
        return bool(self.buttons & BUTTONS[name])


def parse(data: bytes) -> VitaState:
    buttons, lx, ly, rx, ry, tx, ty, click = _PACKET.unpack(data)
    return VitaState(
        buttons=buttons,
        lx=lx,
        ly=ly,
        rx=rx,
        ry=ry,
        touch_x=tx,
        touch_y=ty,
        front_touch=bool(click & FLAG_FRONT_TOUCH),
        rear_left=bool(click & FLAG_REAR_LEFT),
        rear_right=bool(click & FLAG_REAR_RIGHT),
    )


def pack(state: VitaState) -> bytes:
    """Inverso de parse(). Usado pelo Vita falso e pelos testes."""
    click = (
        (FLAG_FRONT_TOUCH if state.front_touch else 0)
        | (FLAG_REAR_LEFT if state.rear_left else 0)
        | (FLAG_REAR_RIGHT if state.rear_right else 0)
    )
    return _PACKET.pack(
        state.buttons, state.lx, state.ly, state.rx, state.ry,
        state.touch_x, state.touch_y, click,
    )
