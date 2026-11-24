# vita-remote: cliente Linux

Recebe os comandos do PS Vita pela rede e cria **gamepad, teclado e mouse virtuais** no Linux com o `uinput` do kernel. Por isso funciona no X11 e no Wayland, em jogos, Steam, emuladores e no desktop.

Funciona com dois apps no Vita:

- **Vita Remote** (`Server/` deste repositório, protocolo v2): é o padrão. Tem três modos (gamepad, mouse e teclado), descoberta automática, reconexão e envio a 120 Hz.
- **VitaPad original** (v1.3): use `--v1`.

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

1. Instale o `VitaRemote.vpk` no Vita (VitaShell) e abra o app.
2. No PC, dentro desta pasta:

```bash
python3 -m vitaremote
```

O cliente acha o Vita sozinho na rede. Se a descoberta não funcionar (alguns firewalls bloqueiam broadcast), passe o IP que aparece no rodapé do app:

```bash
python3 -m vitaremote 192.168.18.40
```

Troque o modo no Vita tocando nas abas do topo ou com **SELECT+START**. Se o PC desconectar, o app volta sozinho a esperar; é só rodar o cliente de novo.

Para usar o VitaPad original:

```bash
python3 -m vitaremote --v1 192.168.18.40
```

### Rede e firewall

| Porta | Direção | Uso |
|---|---|---|
| TCP 5001 | PC → Vita | controle: handshake, troca de modo, teclas do teclado visual, ping |
| UDP 5002 | Vita → broadcast | descoberta (1x/s enquanto ninguém conectou) |
| UDP 5003 → porta aleatória do PC | Vita → PC | estado dos botões, analógicos e toques (120x/s) |

Você não precisa abrir portas no firewall do PC. A cada segundo o cliente manda um pacote UDP para a porta 5003 do Vita, e assim o firewall trata o estado que chega como resposta. Só a descoberta automática pode ser bloqueada; nesse caso, passe o IP.

Opções úteis:

| Opção | O que faz |
|---|---|
| `-P desktop` | escolhe o perfil (`--list-profiles` mostra os prontos; padrão: `padrao`) |
| `-P ~/meu-perfil.toml` | usa um perfil seu |
| `--dry-run` | não cria dispositivos, só mostra no terminal o que seria enviado |
| `--check` | só valida o perfil |
| `--hz 120` | pacotes de estado por segundo (30 a 250) |
| `--v1` | fala com o VitaPad original |
| `-v` | log detalhado |

Para instalar como comando `vita-remote`:

```bash
pipx install --system-site-packages .
```

### Limitação do VitaPad original

O servidor v1.3 **não aceita uma segunda conexão**: se o cliente fechar, é preciso **fechar e abrir o VitaPad no Vita** antes de conectar de novo. O Vita Remote não tem esse problema.

## Perfis

| Perfil | Para quê |
|---|---|
| `padrao` (padrão) | Um mapeamento por modo do Vita Remote: **gamepad** Xbox; **mouse** com touchpad, rolagem e clique direito com 2 dedos; **teclado** com Enter/Backspace/setas nos botões. |
| `gamepad` | Controle estilo **Xbox 360** (Steam/SDL reconhecem sozinhos). Tela frontal = touchpad, toque traseiro = LT/RT. |
| `desktop` | Usar o PC no sofá: analógico esquerdo = mouse, direito = rolagem, X/O = cliques, L/R = voltar/avançar. |
| `teclado-wasd` | Igual ao mapeamento padrão do VitaPad original (WASD/IJKL), para jogos que só aceitam teclado. |

Os perfis de um modo só (`gamepad`, `desktop`, `teclado-wasd`) valem igual para os três modos. Para ter um mapeamento por modo, coloque cada configuração numa seção com o nome do modo, como em [`padrao.toml`](vitaremote/profiles/padrao.toml): `[gamepad.buttons]`, `[mouse.touch]`, `[keyboard.sticks]` etc.

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
two_finger_scroll = true    # 2 dedos arrastando = rolar (só Vita Remote)
two_finger_tap = "mouse:RIGHT"
scroll_sensitivity = 0.03

[cursor]
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

Vita falso, que imita o app Vita Remote com um roteiro dos três modos:

```bash
python3 tools/mock_vita_v2.py
```

```bash
python3 -m vitaremote 127.0.0.1 --dry-run
```

Para imitar o VitaPad original, use `tools/mock_vita.py` e conecte com `--v1`. Encerre o Vita falso quando terminar: enquanto estiver aberto, ele se anuncia na rede e a descoberta automática pode achar ele em vez do Vita de verdade.

Organização do código:

```
vitaremote/
  protocol_v2.py  protocolo do Vita Remote (espelho de Server/source/protocol.h)
  net_v2.py       descoberta, handshake TCP, estado por UDP, reconexão
  protocol_v1.py  formato do pacote do VitaPad original (16 bytes)
  net_v1.py       conexão com o VitaPad original
  profile.py      leitura e validação dos perfis TOML (um ou três modos)
  mapper.py       estado do Vita -> eventos (sem depender de uinput: testável)
  devices.py      dispositivos uinput reais + saída de --dry-run
  __main__.py     linha de comando e troca de modo
```
