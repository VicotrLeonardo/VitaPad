"""Conexão com o servidor VitaPad original (TCP, pergunta-resposta).

Limitação do servidor original (v1.3): depois que um cliente desconecta, a thread
de rede do Vita fica presa num loop com o socket morto e nunca mais chama accept().
Ou seja: para reconectar é preciso reabrir o app no Vita. Isso será corrigido no
servidor novo (Fase 2).
"""

from __future__ import annotations

import logging
import socket
import time
from typing import Callable

from .protocol_v1 import PACKET_SIZE, PORT, REQUEST, VitaState, parse

log = logging.getLogger(__name__)

CONNECT_TIMEOUT = 3.0
RECV_TIMEOUT = 2.0
RETRY_DELAY = 2.0


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise ConnectionError("o Vita fechou a conexão")
        buf += chunk
    return bytes(buf)


def connect(host: str, port: int = PORT) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.settimeout(RECV_TIMEOUT)
    return sock


def poll(sock: socket.socket) -> VitaState:
    sock.sendall(REQUEST)
    return parse(_recv_exact(sock, PACKET_SIZE))


def run(
    host: str,
    port: int,
    on_state: Callable[[VitaState, float], None],
    on_disconnect: Callable[[], None],
    hz: float = 120.0,
    should_stop: Callable[[], bool] = lambda: False,
) -> None:
    """Loop principal: conecta, lê o estado em `hz` vezes/s e reconecta sozinho."""
    period = 1.0 / hz
    while not should_stop():
        try:
            log.info("conectando em %s:%d ...", host, port)
            sock = connect(host, port)
        except OSError as exc:
            log.warning("sem conexão com o Vita (%s). Tentando de novo em %.0fs", exc, RETRY_DELAY)
            time.sleep(RETRY_DELAY)
            continue

        log.info("conectado! (Ctrl+C para sair)")
        frames = 0
        stats_at = last = time.monotonic()
        try:
            with sock:
                while not should_stop():
                    state = poll(sock)
                    now = time.monotonic()
                    on_state(state, now - last)
                    last = now
                    frames += 1
                    if now - stats_at >= 10:
                        log.debug("%.0f leituras/s", frames / (now - stats_at))
                        frames, stats_at = 0, now
                    spare = period - (time.monotonic() - now)
                    if spare > 0:
                        time.sleep(spare)
        except socket.timeout:
            log.warning(
                "o Vita não respondeu. O VitaPad original não aceita uma segunda conexão: "
                "feche e abra o app no Vita para reconectar."
            )
        except (OSError, ConnectionError) as exc:
            log.warning("conexão perdida (%s). Reconectando...", exc)
        finally:
            on_disconnect()
        time.sleep(0.5)

