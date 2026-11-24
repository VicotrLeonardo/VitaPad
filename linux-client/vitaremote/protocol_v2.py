"""Protocolo v2 do Vita Remote. Espelho de Server/source/protocol.h: mudou lá, mude aqui.

  Descoberta  UDP broadcast  Vita -> :5002   Beacon, 1x/s enquanto ninguém conectou
  Controle    TCP 5001       mensagens [u8 tipo][u8 tamanho][payload]
  Estado      UDP            Vita -> PC (porta enviada no HELLO), 30 bytes, `hz` vezes/s
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .protocol_v1 import TOUCH_W, VitaState

MAGIC = b"VRMT"
VERSION = 2
TCP_PORT = 5001
DISCOVERY_PORT = 5002
VITA_UDP_PORT = 5003
PUNCH = b"VRMT"  # datagrama que abre o firewall do PC para o estado do Vita

MODE_GAMEPAD, MODE_MOUSE, MODE_KEYBOARD = 0, 1, 2
MODE_NAMES = ("gamepad", "mouse", "keyboard")

MSG_HELLO = 0x01
MSG_PING = 0x02
MSG_WELCOME = 0x81
MSG_KEY = 0x82
MSG_MODE = 0x83
MSG_PONG = 0x84
MSG_ERROR = 0x8F

ERRORS = {1: "magic inválido", 2: "versão de protocolo incompatível (atualize o app ou o cliente)"}

_HELLO = struct.Struct("<4sBBH")
_WELCOME = struct.Struct("<4sBB")
_KEY = struct.Struct("<HB")
_BEACON = struct.Struct("<4sBH")
_STATE = struct.Struct("<BBHI4BBB8H")
STATE_SIZE = _STATE.size  # 30
UDP_STATE = 1
MAX_POINTS = 2


class ProtocolError(ValueError):
    pass


def frame(msg_type: int, payload: bytes = b"") -> bytes:
    return bytes((msg_type, len(payload))) + payload


def hello(hz: int, udp_port: int) -> bytes:
    return frame(MSG_HELLO, _HELLO.pack(MAGIC, VERSION, hz, udp_port))


def ping() -> bytes:
    return frame(MSG_PING)


@dataclass(frozen=True)
class Message:
    type: int
    payload: bytes


class FrameReader:
    """Junta os bytes que chegam pelo TCP e devolve mensagens completas."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[Message]:
        self._buf += data
        out = []
        while len(self._buf) >= 2:
            size = self._buf[1]
            if len(self._buf) < 2 + size:
                break
            out.append(Message(self._buf[0], bytes(self._buf[2:2 + size])))
            del self._buf[:2 + size]
        return out


def parse_welcome(payload: bytes) -> int:
    magic, version, mode = _WELCOME.unpack(payload[:_WELCOME.size])
    if magic != MAGIC:
        raise ProtocolError("resposta não é de um Vita Remote")
    if version != VERSION:
        raise ProtocolError(f"o app do Vita usa o protocolo v{version}, este cliente usa v{VERSION}")
    return mode


def parse_key(payload: bytes) -> tuple[int, bool]:
    code, pressed = _KEY.unpack(payload[:_KEY.size])
    return code, bool(pressed)


def parse_beacon(data: bytes) -> int | None:
    """Devolve a porta TCP anunciada, ou None se não for um beacon válido."""
    if len(data) < _BEACON.size:
        return None
    magic, version, port = _BEACON.unpack(data[:_BEACON.size])
    if magic != MAGIC or version != VERSION:
        return None
    return port


def beacon(tcp_port: int = TCP_PORT) -> bytes:
    return _BEACON.pack(MAGIC, VERSION, tcp_port)


def parse_state(data: bytes) -> tuple[int, VitaState]:
    """Devolve (seq, estado). Lança ProtocolError se o pacote não for de estado."""
    if len(data) != STATE_SIZE or data[0] != UDP_STATE:
        raise ProtocolError("pacote UDP desconhecido")
    (_, mode, seq, buttons, lx, ly, rx, ry, nf, nr, *coords) = _STATE.unpack(data)
    front = tuple((coords[2 * i], coords[2 * i + 1]) for i in range(min(nf, MAX_POINTS)))
    rear = [(coords[4 + 2 * i], coords[5 + 2 * i]) for i in range(min(nr, MAX_POINTS))]
    first = front[0] if front else (0, 0)
    state = VitaState(
        buttons=buttons, lx=lx, ly=ly, rx=rx, ry=ry,
        touch_x=first[0], touch_y=first[1], front_touch=bool(front),
        rear_left=any(x < TOUCH_W // 2 for x, _ in rear),
        rear_right=any(x >= TOUCH_W // 2 for x, _ in rear),
        front_points=front, mode=mode,
    )
    return seq, state


def pack_state(seq: int, state: VitaState, rear: tuple[tuple[int, int], ...] = ()) -> bytes:
    """Inverso de parse_state (para o Vita falso e testes)."""
    front = list(state.front_points)[:MAX_POINTS]
    rear_l = list(rear)[:MAX_POINTS]
    coords: list[int] = []
    for pts in (front, rear_l):
        padded = pts + [(0, 0)] * (MAX_POINTS - len(pts))
        for x, y in padded:
            coords += [x, y]
    return _STATE.pack(UDP_STATE, state.mode, seq & 0xFFFF, state.buttons,
                       state.lx, state.ly, state.rx, state.ry, len(front), len(rear_l), *coords)


def is_newer(seq: int, last: int | None) -> bool:
    """Compara números de sequência de 16 bits considerando a volta (65535 -> 0)."""
    if last is None:
        return True
    diff = (seq - last) & 0xFFFF
    return 0 < diff < 0x8000
