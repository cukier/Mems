# INVOKE Band — Especificação BLE (protocolo v2)

Documento de referência do protocolo entre o **app do professor** (Web
Bluetooth, `apps/invoke-web`) e as **pulseiras** (ESP32-C3 / NimBLE,
`firmware/`). Qualquer firmware que seguir esta especificação funciona com o app
sem alterações.

Substitui a v1 (flood-mesh de 11 bytes no advertising legado) — ela não
carregava o texto da pergunta. `docs/FIRMWARE_BLE_STATUS.md` fica como registro
histórico dos fixes de conexão.

---

## 1. Arquitetura

```
[ App Web (Chrome/Android, Web Bluetooth) ]
        |  1 conexão GATT — Nordic UART Service (NUS)
[ Pulseira "proxy" ]
        |  re-transmite a pergunta em advertising estendido (BLE 5)
        v
[ Pulseiras 1..64 da turma ]  --(resposta em advertising estendido)-->  proxy --(GATT notify)--> app
```

- O navegador mantém **uma** conexão GATT, com a pulseira que ele escolher no
  seletor (o "proxy" daquela rodada).
- O proxy recebe a pergunta pelo GATT e **re-transmite** por **advertising
  estendido** (fragmentada em chunks). As outras pulseiras escutam, rodam a
  rodada localmente e transmitem a própria resposta do mesmo jeito.
- O proxy remonta as respostas e as repassa ao app por GATT notify.
- Topologia estrela, alcance de ~1 sala, **sem multi-hop**.

---

## 2. Camada GATT — Nordic UART Service (NUS)

| Item | UUID |
|---|---|
| Serviço | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (app → proxy, **write** / write-no-rsp) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (proxy → app, **notify** + CCCD `0x2902`) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Regras obrigatórias:

1. O **UUID do serviço** deve estar no pacote de advertising (lista incompleta
   de UUIDs 128-bit, AD type `0x06`) — é assim que o Chrome e o nRF Connect
   listam a pulseira.
2. **Sem bonding/pairing** (`CONFIG_BT_NIMBLE_MAX_BONDS=0`).
3. Endereço BLE **aleatório estático, novo a cada boot** (evita cache GATT
   velho no Android).
4. Nome do advertising: **`INVOKE-xx`** (`xx` = número da pulseira, 2 dígitos).
   O app extrai o número daí.
5. MTU preferido 512.

### 2.1 App → proxy (RX) — disparo de pergunta

```json
{
  "t": "q",
  "rid": 1737,          // id da rodada (o app gera; 0..65535)
  "cd": 3,              // contagem regressiva (s) antes de abrir a resposta
  "to": 15,             // janela de resposta (s) depois da contagem
  "s": "Qual é a capital do Brasil?",
  "a": "São Paulo",     // opção A  (mostrada em "cima")
  "b": "Rio de Janeiro",// opção B  ("baixo")
  "c": "Brasília",       // opção C  ("esquerda")
  "d": "Salvador",       // opção D  ("direita")
  "bands": [3, 7]       // opcional; ausente/vazio = todas as pulseiras
}
```

O gabarito **nunca** é enviado às pulseiras.

### 2.2 Proxy → app (TX notify) — resposta de uma pulseira

```json
{ "t": "a", "rid": 1737, "n": 3, "ans": "C" }
```

- `n`: número da pulseira.
- `ans`: `"A" | "B" | "C" | "D"`, ou `""` se a pulseira travou sem resposta.

---

## 3. Camada mesh — advertising estendido (BLE 5)

Transporte: **manufacturer data** (AD type `0xFF`), company ID `0xFFFF`, numa
instância de advertising **estendido não-conectável** (instância 1). A
instância 0 continua sendo o advertising legado conectável (flags + UUID NUS +
nome) — nunca sai do ar, então o seletor do Chrome sempre lista `INVOKE-xx`.

### 3.1 Frame

```
0xFF 0xFF | 'I' 'V' | type(1) | rid(2, LE) | total_chunks(1) | chunk_idx(1) | payload_len(1) | payload…
```

- `type`: `'Q'` (pergunta) ou `'A'` (resposta).
- Cada chunk que não é o último tem `payload_len == 180`; o último tem o resto.
- **`'Q'`**: o JSON da §2.1, fatiado em chunks de ≤180 B. O proxy cicla por
  todos os índices durante ~2000 ms (≈55 ms por set) e para.
- **`'A'`**: o JSON da §2.2 (~40 B, 1 chunk), transmitido por ~1200 ms com
  jitter inicial de 10–50 ms.
- Remontagem: uma rodada em voo por vez; a pulseira junta os chunks por `rid` e
  age quando tem todos os índices. Dedupe de pergunta por `rid`; dedupe de
  resposta por `(rid, n)`.

### 3.2 Scan

- Passivo, **sem filtro de duplicatas** (`CONFIG_BT_CTRL_BLE_SCAN_DUPL=n`).
- Pulseira **ociosa** (não é proxy, sem rodada): scan com **duty-cycle ~10%**
  (janela 30 ms / intervalo 300 ms) — leve o bastante pra não estragar o
  estabelecimento de conexão de entrada (a causa do `0x3e` na v1), mas
  suficiente pra ouvir o início de uma rodada. **Ajustar em hardware.**
- Pulseira **em rodada** ou **conectada (proxy)**: scan **contínuo**; as
  pulseiras que não são o proxy soltam o advertising conectável (instância 0)
  durante a rodada e o restauram ao voltar pro WAIT.

---

## 4. Máquina de estados da pulseira

```
WAIT ──(pergunta)──> COUNTDOWN ──(cd s)──> ANSWER ──(to s)──> RESULT ──(~3 s)──> WAIT
```

1. **WAIT** — tela: nome `INVOKE-xx` grande + UUID do serviço NUS + "AGUARDANDO
   PERGUNTA".
2. **COUNTDOWN** — tela: enunciado (com quebra de linha) + as 4 opções em lista
   + contador `cd → 1`.
3. **ANSWER** — mesma tela; a opção pra qual o pulso está inclinado é destacada
   **em tempo real** (ver §5), + barra de tempo encolhendo. A opção mantida no
   instante em que o tempo zera é a resposta (zona morta central = sem
   resposta).
4. **RESULT** — tela: "SUA RESPOSTA" + a letra + o texto da opção, por ~3 s.
   A pulseira transmite a resposta (`'A'` mesh) e, se for o proxy, também
   notifica o app direto.

Cada pulseira conta `cd`+`to` a partir do instante em que remontou a pergunta —
há uma folga de ≤~1–2 s entre pulseiras. Aceitável na v1; um campo de
"deadline" comum resolve depois.

---

## 5. Inclinação → resposta (mapa fixo)

Roll/pitch absolutos (referenciados à gravidade, filtro complementar em
`firmware/main/orientation.c`).

| Inclinação | Opção |
|---|---|
| cima    | **A** |
| baixo   | **B** |
| esquerda| **C** |
| direita | **D** |
| centro (zona morta) | — (sem resposta) |

- `TILT_THRESH_DEG` (default 25°): passa disso no eixo dominante → seleciona.
- `TILT_DEADZONE_DEG` (default 12°): abaixo disso nos dois eixos → limpa a
  seleção. Entre a zona morta e o limiar → mantém a seleção atual (histerese).
- Limiares e sinais são `#define` — **ajustar num pulso de verdade**.

---

## 6. Checklist de conformidade do firmware

- [ ] Serviço NUS com os 3 UUIDs exatos (RX write, TX notify + CCCD).
- [ ] UUID NUS no advertising legado (instância 0, sempre no ar); nome
      `INVOKE-xx`.
- [ ] Sem bonding; endereço aleatório novo a cada boot.
- [ ] Instância 1 estendida não-conectável com o frame da §3.1.
- [ ] Remontagem por `rid`; dedupe `Q` por `rid`, `A` por `(rid, n)`.
- [ ] Scan sem dedupe; duty-cycle ~10% ocioso / contínuo em rodada e no proxy.
- [ ] Proxy: RX GATT → roda local (se endereçado) **e** re-transmite `Q`.
- [ ] Proxy: `A` remontado (ou próprio) → TX notify §2.2.
- [ ] Máquina de estados §4 com o timing `cd`/`to`/~3 s.
- [ ] Mapa fixo da §5; número da pulseira em NVS (1..64).
