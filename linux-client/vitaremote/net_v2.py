"""Conexão com o app Vita Remote (protocolo v2): descoberta, handshake TCP e estado por UDP."""

from __future__ import annotations

import logging
import selectors
import socket
import time
from dataclasses import dataclass, field
from typing import Callable

from . import protocol_v2 as p
from .protocol_v1 import VitaState

log = logging.getLogger(__name__)

CONNECT_TIMEOUT = 3.0
PING_INTERVAL = 1.0
PEER_TIMEOUT = 3.0
RETRY_DELAY = 2.0


class SessionError(Exception):
    pass


def discover(timeout: float = 5.0) -> tuple[str, int] | None:
    """Espera um beacon do Vita na rede local. Devolve (IP, porta TCP) ou None."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("", p.DISCOVERY_PORT))
        deadline = time.monotonic() + timeout
        while (left := deadline - time.monotonic()) > 0:
            s.settimeout(left)
            try:
                data, (ip, _) = s.recvfrom(64)
            except socket.timeout:
                break
            tcp_port = p.parse_beacon(data)
            if tcp_port is not None:
                return ip, tcp_port
    return None


@dataclass
class Handlers:
    on_state: Callable[[VitaState, float], None]
    on_key: Callable[[int, bool], None]
    on_mode: Callable[[int], None]
    on_disconnect: Callable[[], None]


@dataclass
class Stats:
    received: int = 0
    dropped_late: int = 0
    lost: int = 0
    last_seq: int | None = None
    since: float = field(default_factory=time.monotonic)

    def reset(self) -> None:
        self.received = self.dropped_late = self.lost = 0
        self.since = time.monotonic()


def _session(host: str, port: int, hz: int, h: Handlers, should_stop: Callable[[], bool],
             established: list[bool]) -> None:
    tcp = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    with tcp, udp:
        tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        udp.bind(("", 0))
        udp.setblocking(False)
        udp.sendto(p.PUNCH, (host, p.VITA_UDP_PORT))
        tcp.sendall(p.hello(hz, udp.getsockname()[1]))

        reader = p.FrameReader()
        deadline = time.monotonic() + CONNECT_TIMEOUT
        welcome = None
        while welcome is None:
            tcp.settimeout(max(0.1, deadline - time.monotonic()))
            data = tcp.recv(256)
            if not data:
                raise SessionError("o Vita fechou a conexão durante o handshake")
            for msg in reader.feed(data):
                if msg.type == p.MSG_ERROR:
                    raise SessionError(p.ERRORS.get(msg.payload[0], "erro desconhecido") if msg.payload else "erro")
                if msg.type == p.MSG_WELCOME:
                    welcome = p.parse_welcome(msg.payload)
        tcp.setblocking(False)
        established[0] = True
        log.info("conectado ao Vita %s (modo inicial: %s). Ctrl+C para sair", host, p.MODE_NAMES[welcome])
        h.on_mode(welcome)

        sel = selectors.DefaultSelector()
        sel.register(tcp, selectors.EVENT_READ, "tcp")
        sel.register(udp, selectors.EVENT_READ, "udp")
        stats = Stats()
        now = time.monotonic()
        last_heard = last_ping = last_state = now

        while not should_stop():
            for key, _ in sel.select(timeout=0.2):
                if key.data == "udp":
                    while True:
                        try:
                            data, (src, _) = udp.recvfrom(64)
                        except BlockingIOError:
                            break
                        if src != host:
                            continue
                        try:
                            seq, state = p.parse_state(data)
                        except p.ProtocolError:
                            continue
                        if not p.is_newer(seq, stats.last_seq):
                            stats.dropped_late += 1
                            continue
                        if stats.last_seq is not None:
                            stats.lost += ((seq - stats.last_seq) & 0xFFFF) - 1
                        stats.last_seq = seq
                        stats.received += 1
                        t = time.monotonic()
                        h.on_state(state, t - last_state)
                        last_state = last_heard = t
                else:
                    data = tcp.recv(4096)
                    if not data:
                        raise SessionError("o Vita fechou a conexão")
                    last_heard = time.monotonic()
                    for msg in reader.feed(data):
                        if msg.type == p.MSG_KEY:
                            h.on_key(*p.parse_key(msg.payload))
                        elif msg.type == p.MSG_MODE and msg.payload:
                            h.on_mode(msg.payload[0])

            now = time.monotonic()
            if now - last_ping >= PING_INTERVAL:
                tcp.sendall(p.ping())
                udp.sendto(p.PUNCH, (host, p.VITA_UDP_PORT))
                last_ping = now
            if now - last_heard > PEER_TIMEOUT:
                raise SessionError("o Vita parou de responder")
            if now - stats.since >= 10:
                elapsed = now - stats.since
                log.debug("%.0f pacotes/s, %d perdidos, %d fora de ordem",
                          stats.received / elapsed, stats.lost, stats.dropped_late)
                if stats.received == 0:
                    log.warning("nenhum pacote UDP chegando: verifique o firewall (ufw) do PC")
                stats.reset()


def run(host: str, port: int, hz: int, handlers: Handlers,
        should_stop: Callable[[], bool] = lambda: False, max_failures: int | None = None) -> None:
    """Conecta, processa e reconecta sozinho até `should_stop()`.

    Com `max_failures`, desiste (retorna) depois de N tentativas seguidas sem nem
    completar o handshake: útil quando o endereço veio da descoberta e pode estar errado.
    """
    failures = 0
    while not should_stop():
        established = [False]
        try:
            log.info("conectando em %s:%d ...", host, port)
            _session(host, port, hz, handlers, should_stop, established)
        except (OSError, SessionError, p.ProtocolError) as exc:
            log.warning("%s. Tentando de novo em %.0fs", exc, RETRY_DELAY)
        finally:
            handlers.on_disconnect()
        failures = 0 if established[0] else failures + 1
        if max_failures is not None and failures >= max_failures:
            return
        if not should_stop():
            time.sleep(RETRY_DELAY)
