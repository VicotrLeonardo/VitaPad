"""vita-remote: usa o PS Vita como gamepad/teclado/mouse no Linux."""

from __future__ import annotations

import argparse
import logging
import sys

from . import __version__, net_v1
from .mapper import Mapper
from .profile import ProfileError, builtin_profiles, load_profile
from .protocol_v1 import PORT


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="vita-remote",
        description="Recebe os comandos do PS Vita (app VitaPad) e cria gamepad/teclado/mouse virtuais via uinput.",
    )
    p.add_argument("host", nargs="?", help="IP mostrado na tela do Vita (ex.: 192.168.18.40)")
    p.add_argument("-p", "--port", type=int, default=PORT, help=f"porta do Vita (padrão {PORT})")
    p.add_argument("-P", "--profile", default="gamepad",
                   help="perfil pronto (gamepad, desktop...) ou caminho de um .toml (padrão: gamepad)")
    p.add_argument("--hz", type=float, default=120.0, help="leituras por segundo (padrão 120)")
    p.add_argument("-n", "--dry-run", action="store_true",
                   help="não cria dispositivos; só mostra no terminal o que seria enviado")
    p.add_argument("--list-profiles", action="store_true", help="lista os perfis prontos e sai")
    p.add_argument("--check", action="store_true", help="valida o perfil e sai")
    p.add_argument("-v", "--verbose", action="store_true", help="mais mensagens de log")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S",
    )
    log = logging.getLogger("vita-remote")

    if args.list_profiles:
        print("\n".join(builtin_profiles()))
        return 0

    try:
        profile = load_profile(args.profile)
    except ProfileError as exc:
        log.error("%s", exc)
        return 2

    devices = profile.needed_devices()
    log.info("perfil '%s' → dispositivos: %s", profile.name, ", ".join(sorted(devices)) or "nenhum")
    if args.check:
        return 0
    if not args.host:
        log.error("informe o IP do Vita (aparece na tela do app VitaPad). Ex.: vita-remote 192.168.18.40")
        return 2
    if not 1 <= args.hz <= 1000:
        log.error("--hz precisa estar entre 1 e 1000")
        return 2

    if args.dry_run:
        from .devices import PrintOutput
        output = PrintOutput()
    else:
        from .devices import UInputOutput, UInputPermissionError
        try:
            output = UInputOutput(devices)
        except UInputPermissionError as exc:
            log.error("%s", exc)
            return 1
        log.info("dispositivos virtuais criados: %s", ", ".join(output.device_names))

    mapper = Mapper(profile, output)
    try:
        net_v1.run(args.host, args.port, mapper.update, mapper.release_all, hz=args.hz)
    except KeyboardInterrupt:
        log.info("saindo...")
    finally:
        mapper.release_all()
        output.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
