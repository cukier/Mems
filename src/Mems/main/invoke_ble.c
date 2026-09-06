#include <inttypes.h>
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
#include "invoke_game.h"

static const char *TAG = "invoke_ble";

// --- Protocol constants (docs/INVOKE_BLE_ESPECIFICACAO.md) -----------------

#define INVOKE_COMPANY_ID 0xFFFF
#define MESH_INITIAL_HOP 2      // spec §3.1; §3.2 doesn't restate a value
                                 // for G, so gestures start with the same
                                 // hop budget as questions.
#define MESH_TX_WINDOW_MS 1200  // spec §3: how long a mesh message occupies
                                 // the advertising payload before the node
                                 // reverts to its normal NUS/name adv.
#define MESH_RELAY_JITTER_MIN_MS 10
#define MESH_RELAY_JITTER_MAX_MS 50
#define Q_DEDUPE_WINDOW_US (3 * 1000 * 1000)  // spec §3.1: 3s dedupe window

// Spec §4: 20-40ms adv interval (units are 0.625ms, so 0x20 = 20ms, 0x40 = 40ms)
// — fast enough for the Chrome device picker to list a node quickly and for
// mesh messages to actually get on the air promptly.
#define MESH_ADV_ITVL_MIN 0x20
#define MESH_ADV_ITVL_MAX 0x40

// The spec's §4 "adv packet" / "scan response" split for where the mesh
// payload lives is internally inconsistent with its own §6.5 pitfall ("the
// NUS UUID briefly leaves the air during a mesh transmission") — a UUID
// that's only ever placed in the primary adv packet can't disappear from
// scan responses. This implementation resolves it the way §6.5 describes:
// the *primary* adv packet is swapped from [flags + NUS UUID] to [mesh
// manufacturer data] for MESH_TX_WINDOW_MS, then swapped back; the scan
// response (name) is left untouched throughout. conn_mode stays
// undirected-connectable the whole time either way, so the node never stops
// being connectable just because a mesh message is in flight.

// 'Q' | cd(1) | hop(1) | bitmap[8] — 11 bytes.
#define Q_PAYLOAD_LEN 11
// 'G' | band(1) | dir(1) | hop(1) | seq(1) — 5 bytes.
#define G_PAYLOAD_LEN 5
// Company ID (2) + larger of the two payloads.
#define MESH_FRAME_MAX_LEN (2 + Q_PAYLOAD_LEN)

#define Q_DEDUPE_CAP 8
#define G_DEDUPE_CAP 16

// --- NUS UUIDs ---------------------------------------------------------
// BLE_UUID128_INIT wants bytes in little-endian wire order, i.e. each
// hyphen-separated group of the UUID string reversed individually — see
// nimble/host/include/host/ble_uuid.h. Service 6e400001-b5a3-f393-e0a9-
// e50e24dcca9e, RX ...002..., TX ...003... (only the first group differs).

static const ble_uuid128_t s_nus_svc_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x40, 0x6e, 0xa3, 0xb5, 0x93, 0xf3,
    0xe0, 0xa9, 0xe5, 0x0e, 0x24, 0xdc, 0xca, 0x9e);
static const ble_uuid128_t s_nus_rx_uuid = BLE_UUID128_INIT(
    0x02, 0x00, 0x40, 0x6e, 0xa3, 0xb5, 0x93, 0xf3,
    0xe0, 0xa9, 0xe5, 0x0e, 0x24, 0xdc, 0xca, 0x9e);
static const ble_uuid128_t s_nus_tx_uuid = BLE_UUID128_INIT(
    0x03, 0x00, 0x40, 0x6e, 0xa3, 0xb5, 0x93, 0xf3,
    0xe0, 0xa9, 0xe5, 0x0e, 0x24, 0xdc, 0xca, 0x9e);

// --- State -----------------------------------------------------------------

static uint8_t s_band_num;
static char s_ble_name[16]; // "INVOKE-xx"

static uint16_t s_tx_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_tx_subscribed;

static uint8_t s_next_gesture_seq;

typedef struct {
    uint8_t len;
    uint8_t data[MESH_FRAME_MAX_LEN];
} mesh_frame_t;

static QueueHandle_t s_mesh_send_q;

typedef struct {
    uint64_t bitmap;
    uint8_t cd;
    int64_t seen_at_us;
    bool valid;
} q_dedupe_entry_t;
static q_dedupe_entry_t s_q_seen[Q_DEDUPE_CAP];

typedef struct {
    uint8_t band;
    uint8_t last_seq;
    bool valid;
} g_dedupe_entry_t;
static g_dedupe_entry_t s_g_seen[G_DEDUPE_CAP];

// A jittered relay in flight: owns its own one-shot timer and deletes both
// itself and the timer from the timer callback once it fires.
typedef struct {
    mesh_frame_t frame;
    esp_timer_handle_t timer;
} relay_job_t;

// --- NVS-backed band number --------------------------------------------

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

uint8_t invoke_band_number(void) {
    return s_band_num;
}

// --- Dedupe tables -----------------------------------------------------

// True if this exact (bitmap, cd) was seen within the last 3s (and marks it
// seen either way, refreshing the timestamp on a genuine repeat).
static bool q_dedupe_check_and_mark(uint64_t bitmap, uint8_t cd) {
    int64_t now = esp_timer_get_time();
    int oldest_idx = 0;
    int64_t oldest_ts = INT64_MAX;
    for (int i = 0; i < Q_DEDUPE_CAP; i++) {
        if (s_q_seen[i].valid && s_q_seen[i].bitmap == bitmap && s_q_seen[i].cd == cd) {
            bool dup = (now - s_q_seen[i].seen_at_us) < Q_DEDUPE_WINDOW_US;
            s_q_seen[i].seen_at_us = now;
            return dup;
        }
        if (!s_q_seen[i].valid) {
            oldest_idx = i;
            oldest_ts = -1;
        } else if (s_q_seen[i].seen_at_us < oldest_ts) {
            oldest_ts = s_q_seen[i].seen_at_us;
            oldest_idx = i;
        }
    }
    s_q_seen[oldest_idx] = (q_dedupe_entry_t){.bitmap = bitmap, .cd = cd, .seen_at_us = now, .valid = true};
    return false;
}

// True if this (band, seq) was already processed. Small per-band table, no
// time window — a band's seq only repeats once its 8-bit counter wraps.
static bool g_dedupe_check_and_mark(uint8_t band, uint8_t seq) {
    int free_slot = -1;
    for (int i = 0; i < G_DEDUPE_CAP; i++) {
        if (!s_g_seen[i].valid) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (s_g_seen[i].band == band) {
            if (s_g_seen[i].last_seq == seq) return true;
            s_g_seen[i].last_seq = seq;
            return false;
        }
    }
    int slot = (free_slot >= 0) ? free_slot : 0;
    s_g_seen[slot] = (g_dedupe_entry_t){.band = band, .last_seq = seq, .valid = true};
    return false;
}

// --- Advertising: normal (NUS discoverable) vs. mesh (carrying a frame) ---

static int gap_event_cb(struct ble_gap_event *event, void *arg); // defined below, needed by adv_start calls here

static void set_adv_normal(void) {
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &s_nus_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 0; // incomplete list, per spec §2 rule 1

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (const uint8_t *)s_ble_name;
    rsp_fields.name_len = strlen(s_ble_name);
    rsp_fields.name_is_complete = 1;

    ble_gap_adv_stop(); // no-op if nothing in flight
    if (ble_gap_adv_set_fields(&fields) != 0) return;
    if (ble_gap_adv_rsp_set_fields(&rsp_fields) != 0) return;

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // always connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = MESH_ADV_ITVL_MIN;
    adv_params.itvl_max = MESH_ADV_ITVL_MAX;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
}

static void set_adv_mesh_frame(const mesh_frame_t *frame) {
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.mfg_data = frame->data;
    fields.mfg_data_len = frame->len;

    ble_gap_adv_stop();
    if (ble_gap_adv_set_fields(&fields) != 0) return;

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = MESH_ADV_ITVL_MIN;
    adv_params.itvl_max = MESH_ADV_ITVL_MAX;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
}

// --- Mesh send task: serializes on-air transmission of one frame at a time,
// each occupying the advertising payload for MESH_TX_WINDOW_MS. -----------

static void mesh_send_task(void *arg) {
    mesh_frame_t frame;
    while (1) {
        if (xQueueReceive(s_mesh_send_q, &frame, portMAX_DELAY) == pdTRUE) {
            set_adv_mesh_frame(&frame);
            vTaskDelay(pdMS_TO_TICKS(MESH_TX_WINDOW_MS));
            set_adv_normal();
        }
    }
}

static void enqueue_mesh_frame(const mesh_frame_t *frame) {
    if (s_mesh_send_q) {
        xQueueSend(s_mesh_send_q, frame, 0); // drop if the queue is full
    }
}

// Relaying gets a random 10-50ms delay before it goes out, to spread out
// nodes that all just heard the same message (spec §3.2).
static void relay_timer_cb(void *arg) {
    relay_job_t *job = (relay_job_t *)arg;
    enqueue_mesh_frame(&job->frame);
    esp_timer_delete(job->timer); // safe to delete from within one's own callback
    free(job);
}

static void schedule_relay(const mesh_frame_t *frame) {
    relay_job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->frame = *frame;

    const esp_timer_create_args_t args = {
        .callback = relay_timer_cb,
        .arg = job,
        .name = "invoke_relay",
    };
    if (esp_timer_create(&args, &job->timer) != ESP_OK) {
        free(job);
        return;
    }
    uint32_t jitter_ms = MESH_RELAY_JITTER_MIN_MS +
                          (esp_random() % (MESH_RELAY_JITTER_MAX_MS - MESH_RELAY_JITTER_MIN_MS + 1));
    esp_timer_start_once(job->timer, (uint64_t)jitter_ms * 1000);
}

// --- Frame encode/decode ------------------------------------------------

static void build_q_frame(mesh_frame_t *out, uint8_t cd, uint8_t hop, uint64_t bitmap) {
    out->len = 2 + Q_PAYLOAD_LEN;
    out->data[0] = INVOKE_COMPANY_ID & 0xFF;
    out->data[1] = (INVOKE_COMPANY_ID >> 8) & 0xFF;
    out->data[2] = 'Q';
    out->data[3] = cd;
    out->data[4] = hop;
    memcpy(&out->data[5], &bitmap, 8); // little-endian, native on esp32c3
}

static void build_g_frame(mesh_frame_t *out, uint8_t band, char dir, uint8_t hop, uint8_t seq) {
    out->len = 2 + G_PAYLOAD_LEN;
    out->data[0] = INVOKE_COMPANY_ID & 0xFF;
    out->data[1] = (INVOKE_COMPANY_ID >> 8) & 0xFF;
    out->data[2] = 'G';
    out->data[3] = band;
    out->data[4] = (uint8_t)dir;
    out->data[5] = hop;
    out->data[6] = seq;
}

static const char *dir_char_to_str(char dir) {
    switch (dir) {
        case 'u': return "up";
        case 'd': return "down";
        case 'l': return "left";
        case 'r': return "right";
        default: return "?";
    }
}

// --- Gesture notify (node -> app, TX characteristic) --------------------

static void notify_gesture(uint8_t band, char dir) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_subscribed) {
        return; // nobody connected/subscribed to hand this to right now
    }

    cJSON *root = cJSON_CreateObject();
    char band_str[4];
    snprintf(band_str, sizeof(band_str), "%u", band);
    cJSON_AddStringToObject(root, "b", band_str);
    cJSON_AddStringToObject(root, "d", dir_char_to_str(dir));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    free(json);
    if (!om) return;

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "gesture notify failed: rc=%d", rc);
    }
}

// --- Mesh receive handling ------------------------------------------------

static void handle_q_frame(uint8_t cd, uint8_t hop, uint64_t bitmap) {
    if (q_dedupe_check_and_mark(bitmap, cd)) return;

    ESP_LOGI(TAG, "Q recv: cd=%u hop=%u bitmap=0x%016" PRIx64, cd, hop, bitmap);

    bool is_member = (bitmap >> (s_band_num - 1)) & 1;
    if (is_member) {
        invoke_game_on_question(cd);
    }

    if (hop == 0) return;
    mesh_frame_t relay;
    build_q_frame(&relay, cd, hop - 1, bitmap);
    schedule_relay(&relay);
}

static void handle_g_frame(uint8_t band, char dir, uint8_t hop, uint8_t seq) {
    if (g_dedupe_check_and_mark(band, seq)) return;

    ESP_LOGI(TAG, "G recv: band=%u dir=%c hop=%u seq=%u", band, dir, hop, seq);
    notify_gesture(band, dir);

    if (hop == 0) return;
    mesh_frame_t relay;
    build_g_frame(&relay, band, dir, hop - 1, seq);
    schedule_relay(&relay);
}

static void handle_mfg_data(const uint8_t *data, uint8_t len) {
    if (len < 3) return;
    uint16_t company = data[0] | ((uint16_t)data[1] << 8);
    if (company != INVOKE_COMPANY_ID) return;

    uint8_t type = data[2];
    if (type == 'Q' && len == 2 + Q_PAYLOAD_LEN) {
        uint8_t cd = data[3];
        uint8_t hop = data[4];
        uint64_t bitmap;
        memcpy(&bitmap, &data[5], 8);
        handle_q_frame(cd, hop, bitmap);
    } else if (type == 'G' && len == 2 + G_PAYLOAD_LEN) {
        handle_g_frame(data[3], (char)data[4], data[5], data[6]);
    }
}

static int scan_event_cb(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }
    if (fields.mfg_data && fields.mfg_data_len > 0) {
        handle_mfg_data(fields.mfg_data, fields.mfg_data_len);
    }
    return 0;
}

static void start_scan(void) {
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;

    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0; // every relay matters, not just the first
    // Duty-cycle the scan (units are 0.625ms): 20ms window every 100ms, so the
    // established GATT link keeps most of the single C3 radio.
    //
    // This scan only ever runs *while an app is connected* (armed in
    // gap_event_cb on CONNECT, cancelled on DISCONNECT). A continuous scan
    // alongside connectable advertising starves the connection-establishment
    // window on the single-antenna C3: inbound connects fail with reason 0x3e
    // (BLE_ERR_CONN_ESTABLISHMENT) before service discovery — both Chrome and
    // nRF Connect just spin on "connecting" and drop. Keeping the radio
    // scan-free until the link is up fixes that; once connected, the node still
    // relays mesh traffic to the app as spec §2.4 requires.
    disc_params.itvl = 160;
    disc_params.window = 32;

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, scan_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

// --- Question command (app -> node, RX characteristic, JSON) ------------

// Accepts each "bands" element as either a JSON string or number.
static uint64_t parse_bands_bitmap(const cJSON *bands) {
    uint64_t bitmap = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, bands) {
        int band = -1;
        if (cJSON_IsString(item)) {
            band = atoi(item->valuestring);
        } else if (cJSON_IsNumber(item)) {
            band = item->valueint;
        }
        if (band >= 1 && band <= 64) {
            bitmap |= (uint64_t)1 << (band - 1);
        }
    }
    return bitmap;
}

static void handle_question_command(const cJSON *root) {
    const cJSON *cd_field = cJSON_GetObjectItemCaseSensitive(root, "cd");
    const cJSON *bands_field = cJSON_GetObjectItemCaseSensitive(root, "bands");
    if (!cJSON_IsNumber(cd_field) || !cJSON_IsArray(bands_field)) {
        ESP_LOGW(TAG, "question command missing cd/bands");
        return;
    }
    uint8_t cd = (uint8_t)cd_field->valueint;
    uint64_t bitmap = parse_bands_bitmap(bands_field);

    // Originating a question is not itself a "relay", so it goes straight
    // to the send queue — but it still needs to be marked seen, otherwise
    // this same node would try to re-relay it the moment it hears its own
    // advertisement echoed back from a neighbor.
    q_dedupe_check_and_mark(bitmap, cd);

    mesh_frame_t frame;
    build_q_frame(&frame, cd, MESH_INITIAL_HOP, bitmap);
    enqueue_mesh_frame(&frame);

    ESP_LOGI(TAG, "Q send: cd=%u bitmap=0x%016" PRIx64, cd, bitmap);
}

static int rx_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 511) return 0;

    char *buf = malloc(len + 1);
    if (!buf) return BLE_ATT_ERR_INSUFFICIENT_RES;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL) != 0) {
        free(buf);
        return BLE_ATT_ERR_UNLIKELY;
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "RX: invalid JSON");
        return 0;
    }

    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (cJSON_IsString(t) && strcmp(t->valuestring, "q") == 0) {
        handle_question_command(root);
    } else {
        ESP_LOGW(TAG, "RX: unknown/missing \"t\"");
    }
    cJSON_Delete(root);
    return 0;
}

static int tx_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return 0; // notify-only; nothing to do for read/write ops
}

// --- GATT service definition (Nordic UART Service) -----------------------

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
            {0}, // no more characteristics
        },
    },
    {0}, // no more services
};

// --- GAP / connection lifecycle -------------------------------------------

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "app connected, conn_handle=%d", s_conn_handle);
                // Now this node is the proxy: start the mesh scan so gestures
                // from the other bands can be relayed to the app (spec §2.4).
                // The scan is off until here — see start_scan() for why.
                start_scan();
            } else {
                set_adv_normal(); // connection attempt failed, keep advertising
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "app disconnected, reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_tx_subscribed = false;
            ble_gap_disc_cancel(); // stop the mesh scan: no app to relay to, and
                                   // a scan-free radio keeps us reliably
                                   // connectable (see start_scan())
            set_adv_normal();      // spec §2 rule 5: return to normal advertising
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_tx_val_handle) {
                s_tx_subscribed = event->subscribe.cur_notify;
            }
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            // Only relevant if something stopped our BLE_HS_FOREVER
            // advertising out from under us (e.g. a stack-level error);
            // the mesh send task and connect/disconnect handlers otherwise
            // own every intentional adv restart.
            set_adv_normal();
            return 0;

        default:
            return 0;
    }
}

// Uses a fresh non-resolvable private random address every boot, instead of
// the chip's fixed public address. Android keys its GATT service-discovery
// cache by address; a node's public address never changes, so a firmware
// update that changes the GATT table (adds/removes services) can leave a
// phone serving a stale cached table for that address instead of
// rediscovering — exactly the "connected but see nothing" failure mode
// spec §6.1 warns about. A random address regenerated on every boot has
// never been seen by any phone before, so there's never a stale entry to
// hit. Nodes are identified by their advertised name/bitmap membership, not
// their BLE address, so a new address each boot costs nothing here.
static void on_sync(void) {
    ble_addr_t addr = {0};
    if (ble_hs_id_gen_rnd(0, &addr) == 0) {
        ble_hs_id_set_rnd(addr.val);
    }
    ble_hs_util_ensure_addr(1);

    uint8_t own_addr_type, addr_val[6] = {0};
    if (ble_hs_id_infer_auto(0, &own_addr_type) == 0 &&
        ble_hs_id_copy_addr(own_addr_type, addr_val, NULL) == 0) {
        ESP_LOGI(TAG, "BLE address (type=%d, random per boot): "
                       "%02x:%02x:%02x:%02x:%02x:%02x",
                 own_addr_type, addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }

    // No scan here on purpose — see start_scan(). It is armed on connect and
    // cancelled on disconnect, so an unconnected node advertises at full rate
    // and is reliably connectable.
    set_adv_normal();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "nimble host reset, reason=%d", reason);
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// --- Public API -------------------------------------------------------

void invoke_mesh_send_gesture(char dir) {
    uint8_t seq = s_next_gesture_seq++;
    g_dedupe_check_and_mark(s_band_num, seq); // mark our own before it can echo back

    mesh_frame_t frame;
    build_g_frame(&frame, s_band_num, dir, MESH_INITIAL_HOP, seq);
    enqueue_mesh_frame(&frame);

    ESP_LOGI(TAG, "G send: band=%u dir=%c seq=%u", s_band_num, dir, seq);
}

esp_err_t invoke_ble_init(void) {
    s_band_num = load_band_number();
    snprintf(s_ble_name, sizeof(s_ble_name), "INVOKE-%02u", s_band_num);

    s_mesh_send_q = xQueueCreate(8, sizeof(mesh_frame_t));
    if (!s_mesh_send_q) return ESP_ERR_NO_MEM;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_ble_name);

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) return ESP_FAIL;

    // gap_event_cb (BLE_GAP_EVENT_CONNECT/DISCONNECT/SUBSCRIBE/ADV_COMPLETE)
    // is wired in via set_adv_normal()'s adv_start callback argument — but
    // that call happens from on_sync(), after the host is already running,
    // so pass it there instead of a global here.

    ESP_LOGI(TAG, "band %u, name %s", s_band_num, s_ble_name);

    xTaskCreate(mesh_send_task, "invoke_mesh_tx", 4096, NULL, 5, NULL);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
