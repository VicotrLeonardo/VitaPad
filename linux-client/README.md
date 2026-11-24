# vita-remote: cliente Linux

Recebe os comandos do PS Vita pela rede e cria **gamepad, teclado e mouse virtuais** no Linux com o `uinput` do kernel. Por isso funciona no X11 e no Wayland, em jogos, Steam, emuladores e no desktop.

> **Fase 1:** este cliente fala o protocolo do **VitaPad original (v1.3)**. Então dá para usar hoje com o VPK oficial. O app novo do Vita, com modos e teclado na tela, vem na Fase 2.

## Requisitos

- Linux com o módulo `uinput` (vem em praticamente toda distro)
- Python ≥ 3.11
- [`python-evdev`](https://python-evdev.readthedocs.io/)

```bash
sudo apt install python3-evdev
```

## Permissões do `/dev/uinput`

Para criar dispositivos virtuais, o seu usuário precisa poder **escrever** em `/dev/uinput`. Teste:

```bash
test -w /dev/uinput && echo "ok, pode escrever" || echo "sem permissão"
```

Em muitas distros com GNOME/KDE isso já funciona: o logind dá acesso temporário à sessão ativa. Para deixar permanente:

```bash
echo 'KERNEL=="uinput", SUBSYSTEM=="misc", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' | sudo tee /etc/udev/rules.d/99-uinput.rules
```

```bash
sudo usermod -aG input $USER
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger /dev/uinput
```

Depois **saia e entre de novo** na sessão para o grupo `input` valer. Se o módulo não carregar no boot:

```bash
echo uinput | sudo tee /etc/modules-load.d/uinput.conf
```

> Por que não precisa de driver? No Windows, o VitaPad depende do ViGEmBus/vJoy para "inventar" um controle. No Linux, o próprio kernel faz isso pelo `uinput`.

## Uso

1. Instale o `VitaPad.vpk` no Vita (VitaShell) e abra o app. Ele mostra o IP.
2. No PC, dentro desta pasta:

```bash
python3 -m vitaremote 192.168.18.40
```

Opções úteis:

| Opção | O que faz |
|---|---|
| `-P desktop` | escolhe o perfil (`--list-profiles` mostra os prontos) |
| `-P ~/meu-perfil.toml` | usa um perfil seu |
| `--dry-run` | não cria dispositivos, só mostra no terminal o que seria enviado |
| `--check` | só valida o perfil |
| `--hz 120` | leituras por segundo |
| `-v` | log detalhado |

Para instalar como comando `vita-remote`:

```bash
pipx install --system-site-packages .
```

### Limitação do VitaPad original

O servidor v1.3 **não aceita uma segunda conexão**: se o cliente fechar, é preciso **fechar e abrir o VitaPad no Vita** antes de conectar de novo. O servidor novo da Fase 2 vai corrigir isso.

## Perfis

| Perfil | Para quê |
|---|---|
| `gamepad` (padrão) | Controle estilo **Xbox 360** (Steam/SDL reconhecem sozinhos). Tela frontal = touchpad, toque traseiro = LT/RT. |
| `desktop` | Usar o PC no sofá: analógico esquerdo = mouse, direito = rolagem, X/O = cliques, L/R = voltar/avançar. |
| `teclado-wasd` | Igual ao mapeamento padrão do VitaPad original (WASD/IJKL), para jogos que só aceitam teclado. |

Para criar o seu, copie um dos arquivos de [`vitaremote/profiles/`](vitaremote/profiles/) e edite. Formato dos alvos:

```toml
[buttons]
cross  = "pad:A"            # botão do gamepad: A B X Y LB RB LT RT BACK START GUIDE LS RS HAT_UP...
circle = "key:ESC"          # tecla (nomes do kernel: ENTER, A, F5, LEFTCTRL, VOLUMEUP...)
square = "key:LEFTCTRL+C"   # combinação
l      = "mouse:LEFT"       # botão do mouse: LEFT RIGHT MIDDLE SIDE EXTRA
r      = "none"

[sticks]
left  = "pad:left"          # pad:left | pad:right | mouse | scroll | keys:CIMA,ESQ,BAIXO,DIR | none
right = "keys:W,A,S,D"
deadzone = 0.12

[touch]
front = "touchpad"          # touchpad | none
sensitivity = 0.6
tap = "mouse:LEFT"          # toque rápido na tela

[mouse]
stick_speed = 1400          # px/s com analógico no máximo (modo "mouse")
scroll_speed = 12
```

Botões do Vita: `cross circle square triangle up down left right l r start select` e `rear_left`/`rear_right` (metade esquerda/direita do toque traseiro). `l3 r3 l1 r1` só existem no PS TV.

A lista completa de nomes de tecla está em `/usr/include/linux/input-event-codes.h`. Use o nome sem o prefixo `KEY_`.

## Desenvolvimento

Testes, sem precisar de Vita nem de `/dev/uinput`:

```bash
python3 -m unittest -v
```

Vita falso, que imita o servidor e roda um roteiro de botões, analógico e toque:

```bash
python3 tools/mock_vita.py
```

```bash
python3 -m vitaremote 127.0.0.1 --dry-run
```

Organização do código:

```
vitaremote/
  protocol_v1.py  formato do pacote do VitaPad original (16 bytes)
  net_v1.py       conexão TCP e loop de leitura
  profile.py      leitura e validação dos perfis TOML
  mapper.py       estado do Vita -> eventos (sem depender de uinput: testável)
  devices.py      dispositivos uinput reais + saída de --dry-run
```
