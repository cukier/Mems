# INVOKE Band — Especificação BLE (implementação de referência)

Documento consolidado do protocolo BLE do INVOKE, alinhado com o app (Web Bluetooth)
e com o firmware ESP32-C3 / NimBLE. Qualquer firmware que seguir esta especificação
funciona com o app sem alterações.

---

## 1. Visão geral da arquitetura

```
[ App Web (Chrome/Android, Web Bluetooth) ]
        |  GATT (1 conexão) — Nordic UART Service (NUS)
[ Nó proxy/gateway ESP32-C3 ]
        |  flooding mesh no advertising (manufacturer data)
[ Pulseiras INVOKE 1..64 da turma ]
```

- O navegador mantém **uma única conexão GATT** com qualquer nó da rede (o "proxy").
- As pulseiras formam uma **rede mesh por flooding no advertising**: cada nó
  retransmite as mensagens que recebe (hop limitado). Não há pareamento BLE
  entre as pulseiras — tudo via advertising + scan passivo.
- O nó conectado ao app também é membro do mesh (anuncia e escaneia ao mesmo
  tempo em que mantém a conexão GATT).

---

## 2. Camada GATT — Nordic UART Service (NUS)

| Item | Valor |
|---|---|
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (app → nó, **write**) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (nó → app, **notify**) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Regras obrigatórias:

1. O **UUID do serviço NUS (128 bits) deve estar no pacote de advertising**
   (lista incompleta de UUIDs 128-bit, AD type `0x06`). É assim que o seletor
   do Chrome e o nRF Connect identificam a pulseira como INVOKE.
2. **Sem bonding/pairing** (`CONFIG_BT_NIMBLE_MAX_BONDS=0`) — o prompt de
   pareamento quebra o fluxo Web Bluetooth e causa GATT 133 no Android.
3. MTU preferido: **512**.
4. O nó continua anunciando e escaneando o mesh **enquanto o GATT está
   conectado** (a conexão com o app não interrompe o papel mesh).
5. Ao desconectar, o nó retorna ao advertising normal.

### 2.1 App → nó (NUS RX, JSON)

Disparo de questão (o gabarito **nunca** vai para as pulseiras):

```json
{
  "t": "q",                  // tipo: disparo de questão
  "bands": ["1", "3", "7"],  // números das pulseiras da turma (string ou número)
  "cd": 5,                   // contagem regressiva em segundos até o "VÁ"
  "s": "Enunciado da questão (opcional, para o OLED)",
  "o": {                     // opções por direção do gesto
    "up": "texto opção",     // answer_a
    "down": "texto opção",   // answer_b
    "left": "texto opção",   // answer_c
    "right": "texto opção"   // answer_d
  }
}
```

### 2.2 Nó → app (NUS TX, JSON notify)

Gesto capturado por uma pulseira (formato mesh repassado pelo proxy):

```json
{ "b": "3", "d": "up" }
```

- `b`: número da pulseira (string).
- `d`: direção do gesto — `"up" | "down" | "left" | "right"`.

Tolerância (legado): o app também aceita `"3:up"` e `"up"`, mas o formato
JSON acima é o canônico.

---

## 3. Camada Mesh — flooding no advertising

Transporte: **manufacturer data** (AD type `0xFF`), company ID `0xFFFF`.
Quando uma mensagem viaja, o nó troca o advertising por ~1200 ms para
carregá-la e depois volta ao advertising normal (nome + UUID NUS).

### 3.1 Mensagem Q (questão) — 11 bytes de payload

```
'Q' | cd (1B) | hop (1B) | bitmap[8] (little-endian u64)
```

- `cd`: contagem regressiva em segundos.
- `hop`: hops restantes (default inicial **2**); cada retransmissão decrementa.
- `bitmap`: bit `n-1` = pulseira nº `n` (pulseiras 1..64). Só as pulseiras do
  bitmap participam da questão; as demais ignoram.
- Dedupe: mesma chave (bitmap ^ cd) dentro de 3 s não reprocessa/retransmite.

### 3.2 Mensagem G (gesto) — 5 bytes de payload

```
'G' | band (1B) | dir (1B) | hop (1B) | seq (1B)
```

- `band`: número da pulseira (1..64).
- `dir`: `'u' | 'd' | 'l' | 'r'` (up/down/left/right).
- `seq`: contador incremental por pulseira — **dedupe por (band, seq)**:
  cada nó retransmite um gesto uma única vez.
- Ao retransmitir: **jitter de 10–50 ms** antes de anunciar (evita colisão
  quando vários nós retransmitem juntos).

### 3.3 Loop do nó (state machine)

Estados com timing fixo, sincronizados pelo recebimento da mensagem Q:

1. **IDLE** — OLED em standby (`INVOKE-xx`).
2. **COUNTDOWN** — exibe a contagem regressiva `cd` → `3, 2, 1`.
3. **CAPTURE** ("VÁ", 8 s) — janela de captura do gesto do IMU (MPU6050);
   primeiro gesto válido vence. OLED mostra as 4 direções ↔ opções.
4. **ACK/CONFIRM** — mostra a direção registrada (~2 s) e volta a IDLE.

O nó envia o gesto em `mesh_send_gesture` assim que o captura.

---

## 4. Advertising e scan (requisitos exatos)

- **Adv packet (≤31 B)**: flags (`0x02 0x01 0x06`) + lista incompleta de
  UUIDs 128-bit (`0x11 0x06` + 16 B do NUS em little-endian).
- **Scan response (≤31 B)**: nome `INVOKE-xx` (xx = número da pulseira, 2
  dígitos) + manufacturer data opcional (Q ocupa 27 B, G ocupa 20 B — dentro
  do limite de 31).
- **Papéis NimBLE obrigatórios** (todos ativos ao mesmo tempo):
  `ROLE_PERIPHERAL`, `ROLE_BROADCASTER`, `ROLE_OBSERVER`.
- Scan passivo, **sem filtro de duplicatas** (as retransmissões do mesh
  importam).
- Intervalo de advertising normal: `0x20–0x40` (20–40 ms — rápido o bastante
  para o Chrome listar e para o mesh responder rápido).

### sdkconfig.defaults (ESP-IDF, ESP32-C3)

```ini
CONFIG_IDF_TARGET="esp32c3"
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_MAX_BONDS=0
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
CONFIG_FREERTOS_HZ=1000
```

---

## 5. Número da pulseira

- Cada nó tem um **número único 1..64** gravado em NVS (definido na
  gravação/config da placa).
- Deriva o nome BLE (`INVOKE-xx`), a identificação nos gestos (`b`) e a
  participação no bitmap das questões.

---

## 6. Armadilhas conhecidas (Android / Web Bluetooth)

1. **Cache de serviços do Android**: após um reflash, o celular pode devolver
   a tabela GATT antiga (ou vazia). Solução: desligar/religar o Bluetooth
   do celular. O app já reconecta e refaz a descoberta de serviços.
2. **GATT 133 na 1ª tentativa**: normal com ESP32/NimBLE; reconectar 1–2×
   resolve (o app já faz isso).
3. **Scan BLE no Android exige localização ativada** e permissão
   "Dispositivos por perto" para o Chrome.
4. **O UUID NUS no advertising é obrigatório** para a pulseira aparecer no
   seletor do Chrome quando há filtro por serviço — nunca remover do adv.
5. Enquanto uma mensagem mesh ocupa o advertising (1,2 s), o UUID NUS sai do
   ar momentaneamente — normal; os seletores fazem rescan.

---

## 7. Checklist de conformidade do firmware

- [ ] Service NUS com os 3 UUIDs exatos (RX write, TX notify).
- [ ] UUID NUS no advertising; nome `INVOKE-xx` no scan response.
- [ ] Sem bonding (`MAX_BONDS=0`).
- [ ] Mesh: Q (11 B) e G (5 B) em manufacturer data `0xFFFF`, hop inicial 2,
      dedupe (chave Q por 3 s / seq G), jitter 10–50 ms ao retransmitir.
- [ ] Continue anunciando + escaneando com GATT conectado.
- [ ] JSON do app parseado conforme §2.1; gesto reportado como §2.2.
- [ ] Timing: countdown `cd` s, captura 8 s, ack ~2 s.
- [ ] Número da pulseira em NVS (1..64).
