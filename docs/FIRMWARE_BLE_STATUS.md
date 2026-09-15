# Estado do BLE — firmware e cliente web

_Atualizado em 2026-09-06. Leia isto antes de mexer no BLE do app._

## TL;DR

- O **firmware está correto e verificado**. Dois clientes conectam e leem a
  tabela GATT completa: **nRF Connect** e **`apps/invoke-console`** (Web
  Bluetooth, no mesmo Chrome/Android em que o app base44 falha).
- O que ainda falha é o **`src/lib/ble.js` do app base44**, não o firmware.
- A string de erro atual do `ble.js` — _"Isso indica firmware desatualizado: o
  scan contínuo derruba a conexão antes da descoberta de serviços (erro 0x3e).
  Atualize o firmware com o duty-cycle de scan"_ — está **factualmente errada**
  e deve ser removida.

## Layout das setas / mapa direção ↔ letra (o Showcase do base44 está errado)

O display da pulseira (foto real do dispositivo) usa este layout na tela de
CAPTURE ("VÁ!") e no resultado:

```
            A
            ↑
    B  ←    +    →  C
            ↓
            D
```

**`up`=A, `left`=B, `right`=C, `down`=D** — ordem de leitura topo, esquerda,
direita, baixo. Mapa **fixo**.

O Showcase do base44 hoje ilustra `A`=cima, `B`=baixo, `C`=esquerda,
`D`=direita — **trocar** para o mapa acima. `buildDispatch` deve montar `o`
como `{up: A, left: B, right: C, down: D}` e `directionToLetter` deve usar o
mesmo mapa fixo (não inferir por ordem de slot preenchido).

Alinhado no repo: `docs/INVOKE_BLE_ESPECIFICACAO.md` §2.1/§3.3,
`src/Mems/main/invoke_game.c` (`dir_to_letter`, `draw_capture_screen`),
`apps/invoke-console/src/protocol.ts` (`DIR_TO_LETTER`).

## Limite de rádio do ESP32-C3: scan + conexão não coexistem

O C3 tem **um rádio**. Um scan BLE (observer, pro mesh) rodando junto com uma
conexão GATT ativa **mata a conexão** — supervision timeout (`reason=0x208`)
em ~1-2s, mesmo com scan a 20% de duty cycle. Confirmado em log: conexão
segura 5s limpa, o scan liga, cai em 1s.

**Consequência (diverge da spec §2.4):** o nó **para o scan enquanto está
conectado**. Um proxy conectado é periférico puro — **não** retransmite gestos
das outras pulseiras pro app. Regras atuais:

| Estado do nó | Scan |
|---|---|
| Sem app conectado | liga (ouve Q/G do mesh) |
| App conecta (`BLE_GAP_EVENT_CONNECT`) | `ble_gap_disc_cancel()` |
| App desconecta | `start_scan()` de novo |

**O que funciona hoje:** 1 pulseira + app (o proxy roda a própria rodada via
`handle_question_command` → `invoke_game_on_question`, e manda o próprio gesto
por `notify_gesture`).

**O que não funciona:** turma com várias pulseiras + coleta de gestos em tempo
real. O proxy conectado não ouve as bandas 2..N. Precisa de **redesenho** —
modelo poll: o app manda a Q, **desconecta**, o proxy escaneia e bufferiza os
gestos numa característica, o app **reconecta** depois de `to` s pra ler. Isso
mexe no `ble.js` e no `Live.jsx` do base44.

## Contagem: uma fase só (`to`)

O firmware não tem mais fase de contagem pré-"VÁ". Ao receber a Q, a pulseira
mostra **"VÁ!" + `to` contando de `to`→0** e captura o gesto durante toda a
janela. O `cd` do JSON é ignorado pela pulseira (o base44 pode parar de mandar,
ou manter só pra própria UI). Ver `docs/INVOKE_BLE_ESPECIFICACAO.md` §3.3.

## UUIDs canônicos (o cliente deve usar exatamente estes)

| Papel | UUID |
|---|---|
| Serviço NUS | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX — app→nó, WRITE / WRITE NO RESPONSE | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX — nó→app, NOTIFY (+ CCCD 0x2902) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

O `ble.js` já usa esses — manter assim. Formato das mensagens: `docs/INVOKE_BLE_ESPECIFICACAO.md` §2.

## Correções de firmware aplicadas (branch `main`)

| Commit | O quê | Por quê |
|---|---|---|
| `bafa23e` | Scan de mesh só roda com um app conectado (armado no `BLE_GAP_EVENT_CONNECT`, cancelado no `DISCONNECT`). | O scan contínuo (observer) no rádio único do ESP32-C3 matava o estabelecimento da conexão de entrada → `reason 0x3e` (BLE_ERR_CONN_ESTABLISHMENT) antes da descoberta de serviços. |
| `4e38bcb` | Byte order dos 3 UUIDs NUS. | `BLE_UUID128_INIT` recebe o valor de 128 bits em little-endian (os 16 bytes da string invertidos **por completo**), não grupo-a-grupo. Antes o firmware servia `9ecadc24-0ee5-a9e0-f393-b5a36e400001`: o nRF mostrava (não filtra), mas `getPrimaryService('6e400001-…')` do Chrome nunca batia → "serviços vistos: nenhum". |
| `80e48b1` | `CONFIG_BT_NIMBLE_HS_PVCY=n`. | Privacy meio-configurada (IRK não persistia, `MAX_BONDS=0`, endereço estático setado à mão no `on_sync`) quebrava o connect de forma intermitente. |
| _(este)_ | O nó proxy também roda a própria rodada. | `handle_question_command` agora chama `invoke_game_on_question()` se o nó está no bitmap, e `invoke_mesh_send_gesture()` chama `notify_gesture()` do próprio gesto. Sem isso, uma bancada com uma pulseira só (a que está conectada ao app) não entra em CAPTURE nem reporta gesto nenhum. |

Tradeoff conhecido do `bafa23e`: um nó **não** conectado não participa do mesh
(não retransmite flood nem ouve o broadcast `Q`). OK para um gateway + celular;
mesh multi-hop com várias pulseiras precisa de um scan leve sempre-ligado,
ajustado empiricamente — pendente.

## Por que o `src/lib/ble.js` ainda falha

Ele faz `connect()` → `getPrimaryService()` → em falha: `disconnect()` → pausa →
`connect()` de novo, em loop. Esse **churn de connect/disconnect é ele mesmo que
dispara o `0x3e`** (nova tentativa de conexão enquanto o disconnect anterior
ainda está assentando). O `apps/invoke-console/src/ble.ts`, no mesmo aparelho,
faz um `gatt.connect()` limpo + `getPrimaryService()` e conecta de primeira.

## Recomendações para `src/lib/ble.js`

1. Caminho feliz: **um** `gatt.connect()` + `getPrimaryService()`, sem loop de
   disconnect/reconnect.
2. Se precisar de retry: `gatt.disconnect()` completo + pausa **≥ 1000 ms**, no
   máximo 2 tentativas.
3. Não reusar referência de `BluetoothDevice` entre boots do ESP32 — o endereço
   BLE muda a cada boot (aleatório estático).
4. Trocar a string de erro sobre "firmware desatualizado / duty-cycle". Se
   `getPrimaryService` falhar, a mensagem honesta é: _"Conexão instável ou cache
   GATT do Chrome. Reset a permissão Bluetooth do site (config do site →
   Bluetooth) ou desligue/religue o Bluetooth do aparelho e tente de novo."_
5. Referência funcionando: **`apps/invoke-console/src/ble.ts`** — retry limpo,
   e no erro faz dump dos serviços que o aparelho realmente expõe em vez de um
   array vazio silencioso.

## Verificação (2026-09-06, ~15:15)

- **nRF Connect**: conecta na 1ª tentativa. "Nordic UART Service"
  `6e400001-b5a3-f393-e0a9-e50e24dcca9e` com RX (`…400002`, WRITE / WRITE NO
  RESPONSE) e TX (`…400003`, NOTIFY + CCCD `0x2902`).
- **`apps/invoke-console` via Chrome Android**:
  `GATT connect (1/3) → Serviço NUS encontrado → RX/TX resolvidas →
  Notificações TX ativas` e o dispatch `{"t":"q",…}` sai pelo RX.
- **App base44 no mesmo Chrome**: `getPrimaryService` volta vazio; o firmware
  loga `0x3e`.
