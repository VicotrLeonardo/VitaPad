#!/usr/bin/env bash
# Inicia o cliente Vita Remote. Repassa os argumentos (ex.: ./vita-remote.sh -P desktop).
# Usado pelo atalho da Área de Trabalho; também pode ser rodado direto no terminal.

cd "$(dirname "$(readlink -f "$0")")" || exit 1

pause_on_error() {
	echo
	read -rp "O cliente parou com erro. Aperte Enter para fechar..." _
}

if ! python3 -c "import evdev" 2>/dev/null; then
	echo "Falta a biblioteca python3-evdev. Instale com:"
	echo "  sudo apt install python3-evdev"
	pause_on_error
	exit 1
fi

if [ ! -w /dev/uinput ]; then
	echo "Sem permissão de escrita em /dev/uinput (necessária para criar o controle virtual)."
	echo "Veja a seção 'Permissões do /dev/uinput' em linux-client/README.md."
	pause_on_error
	exit 1
fi

echo "Vita Remote: abra o app no PS Vita. Para encerrar, feche esta janela ou aperte Ctrl+C."
echo
python3 -m vitaremote "$@"
status=$?
# 0 = saiu normalmente; 130 = Ctrl+C
if [ "$status" -ne 0 ] && [ "$status" -ne 130 ]; then
	pause_on_error
fi
exit "$status"
