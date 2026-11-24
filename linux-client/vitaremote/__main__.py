"""vita-remote: usa o PS Vita como gamepad/teclado/mouse no Linux."""

from __future__ import annotations

import argparse
import logging
import sys

from . import __version__
from .devices import KEYBOARD_CODES, code_name
from .mapper import Mapper
from .profile import MODES, ProfileError, builtin_profiles, load_profile_set
from . import protocol_v2 as p2

log = logging.getLogger("vita-remote")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="vita-remote",
        description="Recebe os comandos do PS Vita e cria gamepad/teclado/mouse virtuais via uinput.",
    )
    p.add_argument("host", nargs="?",
                   help="IP do Vita. Sem ele, o cliente procura o Vita na rede (só app Vita Remote)")
    p.add_argument("--v1", action="store_true",
                   help="fala com o VitaPad original (v1.3) em vez do app Vita Remote")
    p.add_argument("-p", "--port", type=int, help="porta do Vita (padrão: 5001, ou 5000 com --v1)")
    p.add_argument("-P", "--profile", default=None,
                   help="perfil pronto ou caminho de um .toml (padrão: padrao; com --v1: gamepad)")
    p.add_argument("--hz", type=int, default=120, help="pacotes de estado por segundo (padrão 120)")
    p.add_argument("-n", "--dry-run", action="store_true",
                   help="não cria dispositivos; só mostra no terminal o que seria enviado")
    p.add_argument("--list-profiles", action="store_true", help="lista os perfis prontos e sai")
    p.add_argument("--check", action="store_true", help="valida o perfil e sai")
    p.add_argument("-v", "--verbose", action="store_true", help="mais mensagens de log")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    return p


def make_output(devices: set[str], dry_run: bool):
    if dry_run:
        from .devices import PrintOutput
        return PrintOutput()
    from .devices import UInputOutput
    output = UInputOutput(devices)
    log.info("dispositivos virtuais criados: %s", ", ".join(output.device_names))
    return output


def run_v1(args, profiles) -> None:
    from . import net_v1
    from .protocol_v1 import PORT

    profile = profiles.for_mode("gamepad")
    output = make_output(profile.needed_devices(), args.dry_run)
    mapper = Mapper(profile, output)
    try:
        net_v1.run(args.host, args.port or PORT, mapper.update, mapper.release_all, hz=args.hz)
    finally:
        mapper.release_all()
        output.close()


class V2Controller:
    """Liga o protocolo v2 aos mappers: um por modo, trocando quando o Vita muda de modo."""

    def __init__(self, profiles, output) -> None:
        self.output = output
        self.mappers = {i: Mapper(profiles.for_mode(name), output) for i, name in enumerate(MODES)}
        self.mode = 0
        self.keys_down: set[int] = set()
        self._valid_keys = set(KEYBOARD_CODES)

    def on_mode(self, mode: int) -> None:
        if mode not in self.mappers or mode == self.mode:
            return
        self.mappers[self.mode].release_all()
        self.mode = mode
        log.info("modo: %s", p2.MODE_NAMES[mode])

    def on_state(self, state, dt: float) -> None:
        if state.mode != self.mode:
            self.on_mode(state.mode)
        self.mappers[self.mode].update(state, dt)

    def on_key(self, code: int, pressed: bool) -> None:
        if code not in self._valid_keys:
            log.debug("tecla desconhecida ignorada: %d", code)
            return
        if pressed == (code in self.keys_down):
            return
        (self.keys_down.add if pressed else self.keys_down.discard)(code)
        self.output.set_button("key", code, pressed)
        self.output.sync()
        log.debug("teclado visual: %s %s", code_name(code), "↓" if pressed else "↑")

    def on_disconnect(self) -> None:
        for mapper in self.mappers.values():
            mapper.release_all()
        for code in self.keys_down:
            self.output.set_button("key", code, False)
        self.keys_down.clear()
        self.output.sync()


def run_v2(args, profiles) -> int:
    from . import net_v2

    # O teclado virtual é sempre criado: o teclado visual do Vita manda teclas por ele.
    output = make_output(profiles.needed_devices() | {"key"}, args.dry_run)
    ctl = V2Controller(profiles, output)
    handlers = net_v2.Handlers(ctl.on_state, ctl.on_key, ctl.on_mode, ctl.on_disconnect)
    try:
        if args.host:
            net_v2.run(args.host, args.port or p2.TCP_PORT, args.hz, handlers)
        else:
            while True:
                log.info("procurando o Vita na rede (abra o app Vita Remote)...")
                while not (found := net_v2.discover(timeout=10)):
                    log.warning("ainda não achei o Vita. O app está aberto e na mesma rede? "
                                "Se o firewall bloquear a descoberta, passe o IP do rodapé do app: "
                                "vita-remote 192.168.x.x")
                host, port = found
                log.info("Vita encontrado em %s:%d", host, port)
                net_v2.run(host, args.port or port, args.hz, handlers, max_failures=3)
                log.warning("o aparelho em %s não respondeu; voltando a procurar", host)
    finally:
        ctl.on_disconnect()
        output.close()
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S",
    )

    if args.list_profiles:
        print("\n".join(builtin_profiles()))
        return 0
    if not 30 <= args.hz <= 250:
        log.error("--hz precisa estar entre 30 e 250")
        return 2

    try:
        profiles = load_profile_set(args.profile or ("gamepad" if args.v1 else "padrao"))
    except ProfileError as exc:
        log.error("%s", exc)
        return 2

    for name in MODES if not args.v1 else ("gamepad",):
        devices = profiles.for_mode(name).needed_devices()
        log.info("perfil '%s' [%s] → %s", profiles.name, name, ", ".join(sorted(devices)) or "nada")
    if args.check:
        return 0
    if args.v1 and not args.host:
        log.error("com --v1 informe o IP do Vita (aparece na tela do VitaPad)")
        return 2

    from .devices import UInputPermissionError
    try:
        if args.v1:
            run_v1(args, profiles)
            return 0
        return run_v2(args, profiles)
    except UInputPermissionError as exc:
        log.error("%s", exc)
        return 1
    except KeyboardInterrupt:
        log.info("saindo...")
        return 0


if __name__ == "__main__":
    sys.exit(main())
