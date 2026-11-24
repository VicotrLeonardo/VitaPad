#!/usr/bin/env bash
# Cria o atalho "Vita Remote" no menu de aplicativos e na Área de Trabalho.
# Rode de novo se mover a pasta do projeto (o atalho guarda o caminho absoluto).
#
#   ./install-desktop.sh            instala
#   ./install-desktop.sh --remove   remove os atalhos

set -euo pipefail

here="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
launcher="$here/vita-remote.sh"
icon="$here/assets/vita-remote.png"
apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
name="vita-remote.desktop"

if [ "${1:-}" = "--remove" ]; then
	rm -f "$apps_dir/$name" "$desktop_dir/$name"
	echo "Atalhos removidos."
	exit 0
fi

chmod +x "$launcher"
mkdir -p "$apps_dir"

# Caminhos com espaço precisam de aspas no Exec (especificação .desktop).
cat > "$apps_dir/$name" <<EOF
[Desktop Entry]
Type=Application
Name=Vita Remote
Comment=Usar o PS Vita como mouse, teclado e gamepad
Exec="$launcher"
Icon=$icon
Terminal=true
Categories=Utility;
Keywords=vita;playstation;gamepad;controle;remoto;
EOF
chmod +x "$apps_dir/$name"
echo "Menu de aplicativos: $apps_dir/$name"

if [ -d "$desktop_dir" ]; then
	cp "$apps_dir/$name" "$desktop_dir/$name"
	chmod +x "$desktop_dir/$name"
	# GNOME só executa atalhos da Área de Trabalho marcados como confiáveis.
	gio set "$desktop_dir/$name" metadata::trusted true 2>/dev/null || true
	echo "Área de Trabalho:   $desktop_dir/$name"
fi

command -v update-desktop-database >/dev/null && update-desktop-database "$apps_dir" 2>/dev/null || true
echo "Pronto. Procure por \"Vita Remote\" no menu ou use o ícone na Área de Trabalho."
