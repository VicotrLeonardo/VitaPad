#!/usr/bin/env python3
"""Vita falso do protocolo v2: imita o app Vita Remote para testar sem o aparelho.

Uso:
    python3 tools/mock_vita_v2.py                  # TCP 127.0.0.1:5001 + beacons
    python3 -m vitaremote 127.0.0.1 --dry-run      # em outro terminal (ou sem IP: descoberta)

Roteiro em loop (~9 s): gamepad (X + analógico), mouse (arrasto, rolagem e toque
com 2 dedos) e teclado (digita "oi" e Enter).
"""

from __future__ import annotations

import argparse
import math
import socket
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from vitaremote import protocol_v2 as p  # noqa: E402
from vitaremote.devices import ecodes  # noqa: E402
from vitaremote.protocol_v1 import BUTTONS, VitaState  # noqa: E402

CYCLE = 9.0
TYPED = [ecodes.KEY_O, ecodes.KEY_I, ecodes.KEY_ENTER]


def scripted(t: float) -> VitaState:
    t %= CYCLE
    if t < 3:  # gamepad
        kw: dict = {"mode": p.MODE_GAMEPAD}
        if t < 0.5:
            kw["buttons"] = BUTTONS["cross"]
        elif t < 2:
            a = (t - 0.5) / 1.5 * 2 * math.pi
            kw.update(lx=round(128 + 127 * math.cos(a)), ly=round(128 + 127 * math.sin(a)))
        return VitaState(**kw)
    if t < 6:  # mouse
        kw = {"mode": p.MODE_MOUSE}
        if t < 4:  # 1 dedo arrastando
            kw["front_points"] = ((round(400 + (t - 3) * 800), 500),)
        elif 4.05 <= t < 4.2:  # 1 dedo na faixa de rolagem do meio
            kw.update(front_points=((960, round(300 + (t - 4.05) * 2000)),), scroll_zone=True)
        elif 4.2 <= t < 5:  # 2 dedos descendo = rolar
            y = round(300 + (t - 4.2) * 600)
            kw["front_points"] = ((800, y), (1000, y))
        elif 5.3 <= t < 5.4:  # toque rápido com 2 dedos = clique direito
            kw["front_points"] = ((900, 600), (1100, 600))
        if kw.get("front_points"):
            kw.update(front_touch=True, touch_x=kw["front_points"][0][0], touch_y=kw["front_points"][0][1])
        return VitaState(**kw)
    return VitaState(mode=p.MODE_KEYBOARD)


def _recv_frames(conn: socket.socket, reader: p.FrameReader) -> list[p.Message] | None:
    try:
        data = conn.recv(256)
    except socket.timeout:
        return []
    return reader.feed(data) if data else None


def serve_client(conn: socket.socket, addr, hz_cap: float, stop: threading.Event) -> None:
    reader = p.FrameReader()
    conn.settimeout(3)
    msgs: list[p.Message] = []
    while not msgs:
        got = _recv_frames(conn, reader)
        if not got:
            return
        msgs = got
    hello = msgs[0]
    if hello.type != p.MSG_HELLO:
        return
    magic, version, hz, udp_port = p._HELLO.unpack(hello.payload[:8])
    if magic != p.MAGIC or version != p.VERSION:
        conn.sendall(p.frame(p.MSG_ERROR, bytes([2])))
        return
    conn.sendall(p.frame(p.MSG_WELCOME, p._WELCOME.pack(p.MAGIC, p.VERSION, p.MODE_GAMEPAD)))
    print(f"PC conectado: {addr[0]} (UDP {udp_port}, {hz} Hz)", flush=True)

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    conn.settimeout(0.001)
    start = last_heard = time.monotonic()
    seq, mode, typed_cycle = 0, p.MODE_GAMEPAD, -1
    period = 1 / min(hz, hz_cap)
    try:
        while not stop.is_set():
            now = time.monotonic()
            t = now - start
            state = scripted(t)
            if state.mode != mode:
                mode = state.mode
                conn.sendall(p.frame(p.MSG_MODE, bytes([mode])))
            cycle = int(t // CYCLE)
            if mode == p.MODE_KEYBOARD and t % CYCLE > 6.5 and typed_cycle != cycle:
                typed_cycle = cycle
                for code in TYPED:
                    for pressed in (1, 0):
                        conn.sendall(p.frame(p.MSG_KEY, p._KEY.pack(code, pressed)))
            udp.sendto(p.pack_state(seq, state), (addr[0], udp_port))
            seq += 1
            got = _recv_frames(conn, reader)
            if got is None:
                break
            for msg in got:
                last_heard = now
                if msg.type == p.MSG_PING:
                    conn.sendall(p.frame(p.MSG_PONG))
            if now - last_heard > 3:
                print("PC parou de responder", flush=True)
                break
            time.sleep(period)
    except OSError:
        pass
    finally:
        udp.close()
    print("PC desconectou", flush=True)


def serve(host: str = "127.0.0.1", port: int = p.TCP_PORT, hz_cap: float = 250,
          stop: threading.Event | None = None, beacon: bool = True, ready: threading.Event | None = None) -> None:
    stop = stop or threading.Event()
    connected = threading.Event()

    def beacons() -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as b:
            b.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            while not stop.is_set():
                if not connected.is_set():
                    try:
                        b.sendto(p.beacon(port), ("255.255.255.255", p.DISCOVERY_PORT))
                    except OSError:
                        pass
                stop.wait(1)

    if beacon:
        threading.Thread(target=beacons, daemon=True).start()
    with socket.create_server((host, port)) as srv:
        srv.settimeout(0.2)
        if ready:
            ready.set()
        print(f"Vita falso (v2) escutando em {host}:{port}", flush=True)
        while not stop.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            connected.set()
            with conn:
                serve_client(conn, addr, hz_cap, stop)
            connected.clear()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=p.TCP_PORT)
    ap.add_argument("--no-beacon", action="store_true")
    args = ap.parse_args()
    try:
        serve(args.host, args.port, beacon=not args.no_beacon)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
