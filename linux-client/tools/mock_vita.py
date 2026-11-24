#!/usr/bin/env python3
"""Vita falso: imita o servidor do VitaPad para testar o cliente sem o aparelho.

Uso:
    python3 tools/mock_vita.py                # escuta em 127.0.0.1:5000
    python3 -m vitaremote 127.0.0.1 --dry-run # em outro terminal

Roda um roteiro em loop (~6 s): aperta X, gira o analógico esquerdo,
arrasta o dedo na tela, dá um toque rápido e toca na traseira.
"""

from __future__ import annotations

import argparse
import math
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from vitaremote.protocol_v1 import BUTTONS, PACKET_SIZE, REQUEST, VitaState, pack  # noqa: E402

CYCLE = 6.0


def scripted_state(t: float) -> VitaState:
    t = t % CYCLE
    kw: dict = {}
    if t < 0.5:
        kw["buttons"] = BUTTONS["cross"]
    elif t < 2.0:  # analógico esquerdo dando uma volta
        a = (t - 0.5) / 1.5 * 2 * math.pi
        kw["lx"] = round(128 + 127 * math.cos(a))
        kw["ly"] = round(128 + 127 * math.sin(a))
    elif t < 3.0:  # arrasta o dedo da esquerda para a direita
        kw.update(front_touch=True, touch_x=round(400 + (t - 2.0) * 800), touch_y=500)
    elif 3.5 <= t < 3.6:  # toque rápido (tap)
        kw.update(front_touch=True, touch_x=960, touch_y=544)
    elif 4.0 <= t < 4.5:
        kw["rear_left"] = True
    elif 5.0 <= t < 5.5:
        kw["buttons"] = BUTTONS["start"] | BUTTONS["up"]
    return VitaState(**kw)


def serve(host: str, port: int) -> None:
    with socket.create_server((host, port), reuse_port=False) as srv:
        print(f"Vita falso escutando em {host}:{port} (Ctrl+C para sair)", flush=True)
        while True:
            client, addr = srv.accept()
            print(f"cliente conectado: {addr[0]}", flush=True)
            start = time.monotonic()
            with client:
                try:
                    while True:
                        req = client.recv(len(REQUEST))
                        if not req:
                            break
                        data = pack(scripted_state(time.monotonic() - start))
                        assert len(data) == PACKET_SIZE
                        client.sendall(data)
                except OSError:
                    pass
            print("cliente desconectou", flush=True)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5000)
    args = p.parse_args()
    try:
        serve(args.host, args.port)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
