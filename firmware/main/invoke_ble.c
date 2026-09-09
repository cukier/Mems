#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "invoke_ble.h"
#include "invoke_round.h"
#include "st7735.h" // st7735_ascii_fold for statement/options

static const char *TAG = "invoke_ble";

// --- Advertising instances -----------------------------------------------
// 0: legacy, connectable — flags + NUS UUID + name, always up (the picker).
// 1: extended, non-connectable — chunked round frames, up only during a
//    broadcast window.
#define ADV_INST_LEGACY 0
#define ADV_INST_MESH 1

#define ADV_ITVL_MIN 0x20 // 20 ms
#define ADV_ITVL_MAX 0x40 // 40 ms
#define MESH_ITVL_MIN 0x30
#define MESH_ITVL_MAX 0x60

// --- Mesh frame (manufacturer data, company 0xFFFF) --------------------
//  0xFF 0xFF | 'I' 'V' | type(1) | rid(2 LE) | total(1) | idx(1) | plen(1) | payload
#define MESH_COMPANY_ID 0xFFFF
#define MESH_HDR_LEN 10 // 2 company + 'IV' + type + rid + total + idx + plen
#define MESH_CHUNK_DATA 180
#define MESH_FRAME_MAX (MESH_HDR_LEN + MESH_CHUNK_DATA)
#define ROUND_JSON_MAX 512
#define MESH_MAX_CHUNKS ((ROUND_JSON_MAX + MESH_CHUNK_DATA - 1) / MESH_CHUNK_DATA)

#define Q_TX_WINDOW_MS 2000
#define A_TX_WINDOW_MS 1200
#define CHUNK_GAP_MS 55
#define RELAY_JITTER_MIN_MS 10
#define RELAY_JITTER_MAX_MS 50

// --- NUS UUIDs --------------------------------------------------------
// BLE_UUID128_INIT takes the 128-bit value in little-endian (the 16 bytes of
// the UUID string reversed as one run). 6e400001/2/3-b5a3-f393-e0a9-e50e24dcca9e.
static const ble_uuid128_t s_nus_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_nus_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_nus_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

// --- State -----------------------------------------------------------------

static uint8_t s_band_num;
static bool s_is_gateway; // connectable + no idle scan (see Kconfig)
static char s_ble_name[16]; // "INVOKE-xx"
static uint8_t s_own_addr_type;

static uint16_t s_tx_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_tx_subscribed;

static bool s_round_active; // set by invoke_ble_set_round_active()

static QueueHandle_t s_round_q_in; // reassembled questions -> main loop
static QueueHandle_t s_bcast_q;    // broadcast jobs -> mesh_tx_task

typedef struct {
  uint8_t type; // 'Q' | 'A'
  uint16_t rid;
  uint16_t len;
  char data[ROUND_JSON_MAX];
} bcast_job_t;

// --- question reassembly (single slot) --------------------------------
static struct {
  bool active;
  uint16_t rid;
  uint8_t total;
  uint32_t have_mask;
  uint16_t total_len;
  char buf[ROUND_JSON_MAX + 1];
} s_qra;
static uint16_t s_last_q_rid = 0xFFFF; // already handled / broadcast

// --- answer dedupe (proxy) -------------------------------------------
#define ANS_SEEN_CAP 40
static uint32_t s_ans_seen[ANS_SEEN_CAP];
static int s_ans_seen_n;

static void ans_seen_reset(void) { s_ans_seen_n = 0; }

static bool ans_seen_mark(uint16_t rid, uint8_t band) {
  uint32_t key = ((uint32_t)rid << 8) | band;
  for (int i = 0; i < s_ans_seen_n; i++) {
    if (s_ans_seen[i] == key) return true;
  }
  if (s_ans_seen_n < ANS_SEEN_CAP) {
    s_ans_seen[s_ans_seen_n++] = key;
  } else {
    s_ans_seen[esp_random() % ANS_SEEN_CAP] = key;
  }
  return false;
}

// --- NVS band number --------------------------------------------------

static uint8_t load_band_number(void) {
  nvs_handle_t h;
  uint8_t val = CONFIG_INVOKE_DEFAULT_BAND_NUM;
  if (nvs_open("invoke", NVS_READWRITE, &h) != ESP_OK) {
    return val;
  }
  esp_err_t err = nvs_get_u8(h, "band", &val);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    val = CONFIG_INVOKE_DEFAULT_BAND_NUM;
    nvs_set_u8(h, "band", val);
    nvs_commit(h);
  }
  nvs_close(h);
  if (val < 1 || val > 64) {
    val = CONFIG_INVOKE_DEFAULT_BAND_NUM;
  }
  return val;
}

static bool load_is_gateway(void) {
  nvs_handle_t h;
#ifdef CONFIG_INVOKE_GATEWAY
  uint8_t val = 1;
#else
  uint8_t val = 0;
#endif
  if (nvs_open("invoke", NVS_READONLY, &h) == ESP_OK) {
    uint8_t v;
    if (nvs_get_u8(h, "gw", &v) == ESP_OK) {
      val = v ? 1 : 0;
    }
    nvs_close(h);
  }
  return val != 0;
}

uint8_t invoke_band_number(void) { return s_band_num; }
const char *invoke_ble_name(void) { return s_ble_name; }

// --- Advertising -----------------------------------------------------

static int adv_event_cb(struct ble_gap_event *event, void *arg);

static void legacy_adv_start(void) {
  struct ble_gap_ext_adv_params params;
  memset(&params, 0, sizeof(params));
  params.connectable = s_is_gateway ? 1 : 0; // only the gateway accepts connects
  params.scannable = 1;                      // name still visible either way
  params.legacy_pdu = 1;
  params.own_addr_type = s_own_addr_type;
  params.primary_phy = BLE_HCI_LE_PHY_1M;
  params.secondary_phy = BLE_HCI_LE_PHY_1M;
  params.sid = 0;
  params.itvl_min = ADV_ITVL_MIN;
  params.itvl_max = ADV_ITVL_MAX;

  int rc = ble_gap_ext_adv_configure(ADV_INST_LEGACY, &params, NULL,
                                     adv_event_cb, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "legacy adv configure: %d", rc);
    return;
  }

  // adv data: flags + incomplete 128-bit service UUID list
  uint8_t ad[3 + 2 + 16];
  ad[0] = 2;
  ad[1] = BLE_HS_ADV_TYPE_FLAGS;
  ad[2] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  ad[3] = 1 + 16;
  ad[4] = BLE_HS_ADV_TYPE_INCOMP_UUIDS128;
  memcpy(&ad[5], s_nus_svc_uuid.value, 16);
  struct os_mbuf *om = ble_hs_mbuf_from_flat(ad, sizeof(ad));
  if (om) {
    ble_gap_ext_adv_set_data(ADV_INST_LEGACY, om);
  }

  // scan response: complete local name
  size_t nl = strlen(s_ble_name);
  uint8_t rsp[2 + sizeof(s_ble_name)];
  rsp[0] = (uint8_t)(1 + nl);
  rsp[1] = BLE_HS_ADV_TYPE_COMP_NAME;
  memcpy(&rsp[2], s_ble_name, nl);
  om = ble_hs_mbuf_from_flat(rsp, 2 + nl);
  if (om) {
    ble_gap_ext_adv_rsp_set_data(ADV_INST_LEGACY, om);
  }

  rc = ble_gap_ext_adv_start(ADV_INST_LEGACY, 0, 0);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "legacy adv start: %d", rc);
  }
}

static void mesh_adv_configure(void) {
  struct ble_gap_ext_adv_params params;
  memset(&params, 0, sizeof(params));
  params.connectable = 0;
  params.scannable = 0;
  params.legacy_pdu = 0;
  params.own_addr_type = s_own_addr_type;
  params.primary_phy = BLE_HCI_LE_PHY_1M;
  params.secondary_phy = BLE_HCI_LE_PHY_1M;
  params.sid = 1;
  params.itvl_min = MESH_ITVL_MIN;
  params.itvl_max = MESH_ITVL_MAX;

  int rc = ble_gap_ext_adv_configure(ADV_INST_MESH, &params, NULL, adv_event_cb,
                                     NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "mesh adv configure: %d", rc);
  }
}

// Build one mesh frame AD structure into `out` (returns total bytes).
static size_t build_frame(uint8_t *out, uint8_t type, uint16_t rid,
                          uint8_t total, uint8_t idx, const char *payload,
                          uint8_t plen) {
  uint8_t body = 2 + 2 + 6 + plen; // company + 'IV' + type/rid/total/idx/plen
  out[0] = body;                   // AD length
  out[1] = BLE_HS_ADV_TYPE_MFG_DATA;
  out[2] = MESH_COMPANY_ID & 0xFF;
  out[3] = (MESH_COMPANY_ID >> 8) & 0xFF;
  out[4] = 'I';
  out[5] = 'V';
  out[6] = type;
  out[7] = rid & 0xFF;
  out[8] = (rid >> 8) & 0xFF;
  out[9] = total;
  out[10] = idx;
  out[11] = plen;
  if (plen) {
    memcpy(&out[12], payload, plen);
  }
  return (size_t)body + 1;
}

// Serializes on-air transmission of one broadcast job at a time.
static void mesh_tx_task(void *arg) {
  (void)arg;
  bcast_job_t job;
  uint8_t frame[MESH_FRAME_MAX + 4];

  while (1) {
    if (xQueueReceive(s_bcast_q, &job, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    uint16_t len = job.len;
    if (len > ROUND_JSON_MAX) {
      len = ROUND_JSON_MAX;
    }
    uint8_t nchunks = (uint8_t)((len + MESH_CHUNK_DATA - 1) / MESH_CHUNK_DATA);
    if (nchunks == 0) {
      nchunks = 1;
    }
    uint32_t window = (job.type == 'Q') ? Q_TX_WINDOW_MS : A_TX_WINDOW_MS;

    if (job.type == 'A') {
      vTaskDelay(pdMS_TO_TICKS(RELAY_JITTER_MIN_MS +
                               esp_random() % (RELAY_JITTER_MAX_MS -
                                               RELAY_JITTER_MIN_MS + 1)));
    }

    ble_gap_ext_adv_stop(ADV_INST_MESH);
    bool started = false;
    int64_t end = esp_timer_get_time() + (int64_t)window * 1000;
    while (esp_timer_get_time() < end) {
      for (uint8_t i = 0; i < nchunks; i++) {
        uint16_t off = (uint16_t)i * MESH_CHUNK_DATA;
        uint8_t plen = (uint8_t)((len - off) > MESH_CHUNK_DATA
                                     ? MESH_CHUNK_DATA
                                     : (len - off));
        size_t flen =
            build_frame(frame, job.type, job.rid, nchunks, i, &job.data[off],
                        plen);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, flen);
        if (!om) {
          continue;
        }
        if (ble_gap_ext_adv_set_data(ADV_INST_MESH, om) != 0) {
          continue;
        }
        if (!started) {
          ble_gap_ext_adv_start(ADV_INST_MESH, 0, 0);
          started = true;
        }
        vTaskDelay(pdMS_TO_TICKS(CHUNK_GAP_MS));
      }
    }
    ble_gap_ext_adv_stop(ADV_INST_MESH);
  }
}

static void enqueue_bcast(uint8_t type, uint16_t rid, const char *data,
                          size_t len) {
  if (!s_bcast_q) {
    return;
  }
  bcast_job_t *job = calloc(1, sizeof(*job));
  if (!job) {
    return;
  }
  job->type = type;
  job->rid = rid;
  job->len = (uint16_t)(len > ROUND_JSON_MAX ? ROUND_JSON_MAX : len);
  memcpy(job->data, data, job->len);
  if (xQueueSend(s_bcast_q, job, 0) != pdTRUE) {
    ESP_LOGW(TAG, "bcast queue full, dropped %c", type);
  }
  free(job);
}

// --- Question parse (JSON -> round_q_t) ------------------------------

static bool parse_question(const char *json, round_q_t *out) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    return false;
  }
  const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
  if (!cJSON_IsString(t) || strcmp(t->valuestring, "q") != 0) {
    cJSON_Delete(root);
    return false;
  }

  // optional address filter
  const cJSON *bands = cJSON_GetObjectItemCaseSensitive(root, "bands");
  if (cJSON_IsArray(bands) && cJSON_GetArraySize(bands) > 0) {
    bool mine = false;
    const cJSON *it;
    cJSON_ArrayForEach(it, bands) {
      int n = cJSON_IsNumber(it) ? it->valueint
              : cJSON_IsString(it) ? atoi(it->valuestring)
                                   : 0;
      if (n == s_band_num) {
        mine = true;
      }
    }
    if (!mine) {
      cJSON_Delete(root);
      return false;
    }
  }

  memset(out, 0, sizeof(*out));
  const cJSON *rid = cJSON_GetObjectItemCaseSensitive(root, "rid");
  const cJSON *cd = cJSON_GetObjectItemCaseSensitive(root, "cd");
  const cJSON *to = cJSON_GetObjectItemCaseSensitive(root, "to");
  out->rid = cJSON_IsNumber(rid) ? (uint16_t)rid->valuedouble : 0;
  out->cd = cJSON_IsNumber(cd) ? (uint8_t)cd->valueint : 3;
  out->to = cJSON_IsNumber(to) ? (uint8_t)to->valueint : 10;

  const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "s");
  if (cJSON_IsString(s)) {
    st7735_ascii_fold(out->stmt, sizeof(out->stmt), s->valuestring);
  }
  const char *keys[4] = {"a", "b", "c", "d"};
  for (int i = 0; i < 4; i++) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
    if (cJSON_IsString(o)) {
      st7735_ascii_fold(out->opt[i], sizeof(out->opt[i]), o->valuestring);
    }
  }
  cJSON_Delete(root);
  return true;
}

// A raw question JSON reached us (GATT RX on the proxy, or ext-adv on others).
// `is_proxy` -> also re-broadcast it over the mesh.
static void handle_question_json(const char *json, size_t len, bool is_proxy) {
  round_q_t q;
  bool run = parse_question(json, &q);

  if (is_proxy) {
    // learn the rid so our own ext-adv echo is ignored, then relay
    uint16_t rid = 0;
    cJSON *r = cJSON_Parse(json);
    if (r) {
      const cJSON *ri = cJSON_GetObjectItemCaseSensitive(r, "rid");
      if (cJSON_IsNumber(ri)) {
        rid = (uint16_t)ri->valuedouble;
      }
      cJSON_Delete(r);
    }
    s_last_q_rid = rid;
    ans_seen_reset();
    enqueue_bcast('Q', rid, json, len);
  }

  if (run && s_round_q_in) {
    if (!is_proxy) {
      ans_seen_reset();
    }
    xQueueSend(s_round_q_in, &q, 0);
  }
}

// --- Answer notify (proxy -> app, TX) ------------------------------

static void notify_answer(uint16_t rid, uint8_t band, char letter) {
  if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_subscribed) {
    return;
  }
  char ans[2] = {(letter >= 'A' && letter <= 'D') ? letter : '\0', '\0'};
  cJSON *r = cJSON_CreateObject();
  cJSON_AddStringToObject(r, "t", "a");
  cJSON_AddNumberToObject(r, "rid", rid);
  cJSON_AddNumberToObject(r, "n", band);
  cJSON_AddStringToObject(r, "ans", ans);
  char *j = cJSON_PrintUnformatted(r);
  cJSON_Delete(r);
  if (!j) {
    return;
  }
  struct os_mbuf *om = ble_hs_mbuf_from_flat(j, strlen(j));
  free(j);
  if (!om) {
    return;
  }
  ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
}

static void handle_answer_json(const char *json) {
  if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return; // only the proxy forwards answers
  }
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    return;
  }
  const cJSON *rid = cJSON_GetObjectItemCaseSensitive(root, "rid");
  const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "n");
  const cJSON *ans = cJSON_GetObjectItemCaseSensitive(root, "ans");
  uint16_t r = cJSON_IsNumber(rid) ? (uint16_t)rid->valuedouble : 0;
  uint8_t b = cJSON_IsNumber(n) ? (uint8_t)n->valueint : 0;
  char letter =
      (cJSON_IsString(ans) && ans->valuestring[0]) ? ans->valuestring[0] : 0;
  cJSON_Delete(root);

  if (b == 0 || ans_seen_mark(r, b)) {
    return;
  }
  ESP_LOGI(TAG, "answer band=%u ans=%c -> app", b, letter ? letter : '-');
  notify_answer(r, b, letter);
}

// --- Mesh scan + reassembly ------------------------------------------

static void handle_mesh_ad(const uint8_t *d, uint8_t len) {
  if (len < MESH_HDR_LEN || d[0] != (MESH_COMPANY_ID & 0xFF) ||
      d[1] != ((MESH_COMPANY_ID >> 8) & 0xFF)) {
    return;
  }
  if (d[2] != 'I' || d[3] != 'V') {
    return;
  }
  uint8_t type = d[4];
  uint16_t rid = d[5] | ((uint16_t)d[6] << 8);
  uint8_t total = d[7];
  uint8_t idx = d[8];
  uint8_t plen = d[9];
  const uint8_t *payload = &d[10];
  if (MESH_HDR_LEN + plen > len) {
    return;
  }

  if (type == 'A') {
    char jbuf[128];
    if (plen >= sizeof(jbuf)) {
      return;
    }
    memcpy(jbuf, payload, plen);
    jbuf[plen] = '\0';
    handle_answer_json(jbuf);
    return;
  }
  if (type != 'Q') {
    return;
  }

  if (rid == s_last_q_rid) {
    return; // already handled / it's our own relay
  }
  if (total == 0 || total > MESH_MAX_CHUNKS || idx >= total) {
    return;
  }
  if (!s_qra.active || s_qra.rid != rid) {
    s_qra.active = true;
    s_qra.rid = rid;
    s_qra.total = total;
    s_qra.have_mask = 0;
    s_qra.total_len = 0;
  }
  uint16_t off = (uint16_t)idx * MESH_CHUNK_DATA;
  if (!(s_qra.have_mask & (1u << idx)) &&
      off + plen <= ROUND_JSON_MAX) {
    memcpy(&s_qra.buf[off], payload, plen);
    s_qra.have_mask |= (1u << idx);
    s_qra.total_len += plen;
  }
  if (s_qra.have_mask == ((1u << total) - 1u)) {
    s_qra.buf[s_qra.total_len] = '\0';
    s_qra.active = false;
    s_last_q_rid = rid;
    handle_question_json(s_qra.buf, s_qra.total_len, false);
  }
}

static void parse_ad_structs(const uint8_t *data, uint8_t len) {
  uint8_t i = 0;
  while (i + 1 < len) {
    uint8_t l = data[i];
    if (l == 0 || i + 1 + l > len) {
      break;
    }
    uint8_t type = data[i + 1];
    if (type == BLE_HS_ADV_TYPE_MFG_DATA && l >= 1 + MESH_HDR_LEN) {
      handle_mesh_ad(&data[i + 2], (uint8_t)(l - 1));
    }
    i += 1 + l;
  }
}

static int scan_event_cb(struct ble_gap_event *event, void *arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_EXT_DISC) {
    parse_ad_structs(event->ext_disc.data, event->ext_disc.length_data);
  } else if (event->type == BLE_GAP_EVENT_DISC) {
    parse_ad_structs(event->disc.data, event->disc.length_data);
  }
  return 0;
}

static bool s_scanning;

static void scan_start(void) {
  struct ble_gap_ext_disc_params uncoded;
  memset(&uncoded, 0, sizeof(uncoded));
  uncoded.passive = 1;
  uncoded.itvl = 224; // continuous — the only safe duty on the single C3 radio
  uncoded.window = 224;
  ble_gap_disc_cancel();
  int rc = ble_gap_ext_disc(s_own_addr_type, 0, 0, 0 /*no dup filter*/, 0, 0,
                            &uncoded, NULL, scan_event_cb, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "ext_disc: %d", rc);
    return;
  }
  s_scanning = true;
}

static void scan_stop(void) {
  if (s_scanning) {
    ble_gap_disc_cancel();
    s_scanning = false;
  }
}

// Non-gateway bands scan continuously (never connectable -> no 0x3e). The
// gateway scans only once connected or while a round is running; idle it stays
// scan-free so inbound connects don't get starved (the reason-0x3e failure).
static void scan_refresh(void) {
  bool want = s_is_gateway
                  ? (s_round_active || s_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                  : true;
  if (want) {
    scan_start();
  } else {
    scan_stop();
  }
}

// --- GATT service (Nordic UART Service) --------------------------------

static int rx_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return 0;
  }
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0 || len > ROUND_JSON_MAX) {
    return 0;
  }
  char *buf = malloc(len + 1);
  if (!buf) {
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL) != 0) {
    free(buf);
    return BLE_ATT_ERR_UNLIKELY;
  }
  buf[len] = '\0';
  ESP_LOGI(TAG, "RX %u bytes", len);
  handle_question_json(buf, len, true);
  free(buf);
  return 0;
}

static int tx_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)ctxt;
  (void)arg;
  return 0; // notify-only
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_nus_rx_uuid.u,
                .access_cb = rx_chr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_nus_tx_uuid.u,
                .access_cb = tx_chr_access_cb,
                .val_handle = &s_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

// --- GAP lifecycle -------------------------------------------------------

static int adv_event_cb(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "app connected, conn=%d", s_conn_handle);
      scan_refresh(); // proxy: scan continuously to hear answers
    } else {
      legacy_adv_start();
    }
    return 0;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "app disconnected, reason=%d", event->disconnect.reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_tx_subscribed = false;
    legacy_adv_start();
    scan_refresh();
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    if (event->subscribe.attr_handle == s_tx_val_handle) {
      s_tx_subscribed = event->subscribe.cur_notify;
    }
    return 0;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    if (event->adv_complete.instance == ADV_INST_LEGACY) {
      legacy_adv_start();
    }
    return 0;

  default:
    return 0;
  }
}

static void on_sync(void) {
  // Fresh non-resolvable random address every boot: Android keys its GATT
  // discovery cache by address, so an address no phone has seen can never hit
  // a stale cached (or empty) table for this node.
  ble_addr_t addr = {0};
  if (ble_hs_id_gen_rnd(0, &addr) == 0) {
    ble_hs_id_set_rnd(addr.val);
  }
  ble_hs_util_ensure_addr(1);
  if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
    s_own_addr_type = BLE_OWN_ADDR_RANDOM;
  }

  legacy_adv_start();
  mesh_adv_configure();
  scan_refresh(); // gateway: none while idle. non-gateway: continuous.
}

static void on_reset(int reason) {
  ESP_LOGW(TAG, "nimble host reset, reason=%d", reason);
}

static void host_task(void *param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

// --- Public API -------------------------------------------------------

void invoke_ble_broadcast_question(const char *json, size_t len) {
  uint16_t rid = 0;
  cJSON *r = cJSON_Parse(json);
  if (r) {
    const cJSON *ri = cJSON_GetObjectItemCaseSensitive(r, "rid");
    if (cJSON_IsNumber(ri)) {
      rid = (uint16_t)ri->valuedouble;
    }
    cJSON_Delete(r);
  }
  s_last_q_rid = rid;
  enqueue_bcast('Q', rid, json, len);
}

void invoke_ble_broadcast_answer(uint16_t rid, uint8_t band, char letter) {
  char ans[2] = {(letter >= 'A' && letter <= 'D') ? letter : '\0', '\0'};
  cJSON *r = cJSON_CreateObject();
  cJSON_AddStringToObject(r, "t", "a");
  cJSON_AddNumberToObject(r, "rid", rid);
  cJSON_AddNumberToObject(r, "n", band);
  cJSON_AddStringToObject(r, "ans", ans);
  char *j = cJSON_PrintUnformatted(r);
  cJSON_Delete(r);
  if (j) {
    enqueue_bcast('A', rid, j, strlen(j));
    free(j);
  }
  // If an app is connected to *this* band, hand it our own answer directly.
  if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ans_seen_mark(rid, band);
    notify_answer(rid, band, letter);
  }
}

void invoke_ble_set_round_active(bool active) {
  if (s_round_active == active) {
    return;
  }
  s_round_active = active;
  scan_refresh();
}

bool invoke_ble_take_question(round_q_t *out) {
  if (!s_round_q_in) {
    return false;
  }
  return xQueueReceive(s_round_q_in, out, 0) == pdTRUE;
}

esp_err_t invoke_ble_init(void) {
  s_round_q_in = xQueueCreate(2, sizeof(round_q_t));
  s_bcast_q = xQueueCreate(4, sizeof(bcast_job_t));
  if (!s_round_q_in || !s_bcast_q) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    return err;
  }

  s_band_num = load_band_number();
  s_is_gateway = load_is_gateway();
  snprintf(s_ble_name, sizeof(s_ble_name), "INVOKE-%02u", s_band_num);

  err = nimble_port_init();
  if (err != ESP_OK) {
    return err;
  }

  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.gatts_register_cb = NULL;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set(s_ble_name);

  int rc = ble_gatts_count_cfg(s_gatt_svcs);
  if (rc != 0) {
    return ESP_FAIL;
  }
  rc = ble_gatts_add_svcs(s_gatt_svcs);
  if (rc != 0) {
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "band %u, name %s, gateway=%d", s_band_num, s_ble_name,
           s_is_gateway);

  xTaskCreate(mesh_tx_task, "invoke_mesh_tx", 4096, NULL, 5, NULL);
  nimble_port_freertos_init(host_task);
  return ESP_OK;
}
