# Vita Remote — Plano do projeto

PS Vita como controle remoto completo de um desktop Linux: **gamepad + teclado + mouse**, via Wi-Fi.

## 1. O que o VitaPad original faz (analisado em `referencias/VitaPad`, último commit out/2020)

| Parte | Como funciona hoje | Problema para o nosso objetivo |
|---|---|---|
| Servidor (Vita) `Server/source/main.c` (168 linhas, C + vita2d) | Servidor **TCP na porta 5000**. Fica esperando o PC mandar `"request"` e responde com um `PadPacket` (botões, 4 eixos analógicos, 1 ponto de toque, flags de clique). | Modelo **pergunta-resposta**: cada leitura custa uma ida e volta de rede, o que aumenta a latência. Sem modos, sem teclado na tela. |
| Cliente Linux `Client/main.cpp` (C++) | Usa **X11 + XTest**: aperta teclas do teclado (WASD, IJKL…) e move o mouse com `XWarpPointer`. Mapeamento em `linux.xml` (keysyms do X11). | **Não existe gamepad no Linux** (ViGEm/vJoy são só Windows). Não funciona no Wayland. Precisa recompilar para mudar a lógica. |

Formato do pacote original (útil para a Fase 1, que reaproveita o VPK oficial):

```
uint32 buttons | uint8 lx, ly, rx, ry | uint16 tx, ty | uint8 click   (13 bytes + 3 de padding = 16 bytes)
click: 0x01 = toque frontal (mover mouse), 0x08 = clique esquerdo, 0x10 = clique direito (toque traseiro)
```

**Bug do servidor original:** depois que o PC desconecta, o loop interno de `server_thread` nunca termina (não checa o retorno de `sceNetRecv`/`sceNetSend`), então o Vita nunca volta a chamar `accept()`. Para reconectar é preciso reabrir o app. Além disso, `sceNetRecv(client, unused, 256, 0)` escreve até 256 bytes num buffer de 8 (estouro de pilha). Os dois problemas foram corrigidos no servidor novo (Fase 2).

## 2. Arquitetura proposta

```
 PS Vita (C / VitaSDK / vita2d)                     PC Linux (Python + evdev/uinput)
 ┌──────────────────────────────┐   UDP 5000        ┌──────────────────────────────┐
 │ Loop 60–120 Hz: lê botões,   │ ───────────────▶  │ Recebe estado → aplica        │
 │ analógicos, toques → envia   │  (estado, push)   │ mapeamento → emite eventos   │
 │                              │                   │  • gamepad virtual (Xbox-like)│
 │ Modos: Gamepad/Teclado/Mouse │   TCP 5001        │  • teclado virtual            │
 │ Teclado visual na tela       │ ───────────────▶  │  • mouse virtual              │
 │                              │ (teclas, troca de │                              │
 │                              │  modo, handshake) │ Perfil de mapeamento (TOML)  │
 └──────────────────────────────┘                   └──────────────────────────────┘
```

Decisões:

- **UDP para estado contínuo** (gamepad/mouse): o Vita *empurra* o estado com número de sequência; pacote perdido é substituído pelo próximo.
- **TCP para eventos que não podem se perder**: tecla do teclado visual, troca de modo, handshake de versão.
- **O Vita é "burro", o PC é "esperto"**: o Vita manda o estado cru + modo atual; todo o mapeamento fica no cliente Python. Mudar mapeamento = editar um arquivo, sem recompilar VPK.
- **`python-evdev` em vez de `python-uinput`**: os dois usam o mesmo `/dev/uinput` do kernel, mas o evdev é mantido ativamente e já está instalado nesta máquina (1.7.0). `python-uinput` está praticamente abandonado.
- **Teclado manda código de tecla, não caractere**: o uinput emite *teclas físicas*, e o caractere final depende do layout do PC (ex.: ABNT2). O Vita envia `KEY_*` + modificadores (Shift/AltGr); o cliente resolve acentos via teclas mortas do layout configurado.
- **Descoberta automática** (opcional): o Vita anuncia por broadcast UDP; o cliente acha o IP sozinho.

Troca de modo: o botão PS é do sistema e não pode ser capturado. Proposta: **barra de abas no topo da tela touch** + atalho **Select + Start** (para não perder o Select como botão de gamepad).

## 3. Fases

| Fase | Entrega | Precisa de VitaSDK? |
|---|---|---|
| **0. Preparação** | VitaSDK instalado, Vita com HENkaku/Enso + VitaShell, IP do Vita conhecido | — |
| **1. Cliente Python compatível com o VitaPad original** ✅ | `linux-client/` em Python que fala o protocolo antigo e cria gamepad + mouse via uinput. Testável já com o VPK oficial. Valida a parte Linux sozinha. | Não |
| **2. Protocolo v2 + app Vita com 3 modos** ✅ | Fork do servidor: UDP push, TCP de eventos, UI com abas Gamepad/Teclado/Mouse | Sim |
| **3. Teclado visual** | Layout desenhado com vita2d, toque → tecla, Shift/Caps, feedback visual | Sim |
| **4. Mapeamento flexível** | Perfil TOML por modo: botão do Vita → tecla / botão de mouse / botão de gamepad; recarregar sem reiniciar | Não |
| **5. Documentação** | README: compilar VPK, instalar, regra udev, rodar cliente, systemd user service | — |

## 4. Estado do ambiente (verificado em 24/11/2026)

- Ubuntu 24.04, GNOME em **X11**, Python 3.12, `python-evdev` 1.7.0 instalado.
- `/dev/uinput`: **escrita já funciona** (ACL dada pelo logind à sessão ativa; criação de dispositivo testada com sucesso). Mesmo assim o README vai ensinar a regra udev permanente:
  ```
  # /etc/udev/rules.d/99-uinput.rules
  KERNEL=="uinput", SUBSYSTEM=="misc", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"
  ```
  + `sudo usermod -aG input $USER` e relogar.
- VitaSDK instalada em `~/vitasdk` (via vdpm, GCC 15.2).
- PC na rede `192.168.18.0/24` (Wi-Fi `wlp4s0`, IP 192.168.18.58). IP do Vita ainda não identificado.

- Vita: desbloqueado com **Enso + VitaShell** (instala VPK).
- Teclado do PC: **ABNT2**. Isso importa na Fase 3: o teclado visual manda códigos de tecla, e o mapeamento para `ç`, acentos e teclas mortas precisa seguir o layout ABNT2.

## 5. Repositório

Fork: https://github.com/VicotrLeonardo/VitaPad (`origin`), com o original como `upstream`.

```
CUSTOM PS VITA/VitaPad/
├── PLANO.md
├── Server/          # app do Vita: Vita Remote (C + vita2d)
├── Client/          # cliente C++ original (Windows/X11), mantido como referência
└── linux-client/    # cliente Python novo (uinput)
```

**Licença:** o VitaPad original **não tem arquivo de licença**, o que juridicamente significa "todos os direitos reservados". Antes de divulgar o projeto, vale abrir uma issue pedindo ao autor que adicione uma licença. Outra saída é manter o código novo (`linux-client/`) separado sob uma licença própria.

## 6. Estado atual (24/11/2026)

- **Fase 1 ✅** cliente Python, testado com o VitaPad original no Vita real.
- **Fase 2 ✅** app **Vita Remote** (`Server/`, TITLE_ID `VREM00001`, instala ao lado do VitaPad) com abas GAMEPAD/MOUSE/TECLADO e SELECT+START. Protocolo v2 em `Server/source/protocol.h`: TCP 5001, descoberta UDP 5002, estado UDP 5003. Testado no Vita real: descoberta automática, 115 pacotes/s, 0 perdidos.
- **Fase 3 ✅** interface redesenhada (v0.3+): fonte do sistema, símbolos desenhados, abas Mouse/Teclado/Gamepad (abre no Mouse), faixa de rolagem no meio da aba Mouse e teclado ABNT2 com teclas mortas, modificadores de 1 ou 2 toques, navegação pelo D-pad e prévia local. Protocolo **v3** (campo `flags` no estado). Atalho no PC: `linux-client/install-desktop.sh`.

Compilar o VPK: `export VITASDK=~/vitasdk PATH=~/vitasdk/bin:$PATH && make -C Server`.
