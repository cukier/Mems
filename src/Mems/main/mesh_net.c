#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "mesh_net.h"

static const char *TAG = "mesh_net";

// On-air packet, carried verbatim as the 0xFF (manufacturer data) AD
// structure's payload. Fixed size, little-endian (native on ESP32-C3), no
// padding: 24 bytes, well inside the 31-byte legacy ADV budget alongside the
// flags AD structure NimBLE adds automatically.
typedef struct __attribute__((packed)) {
  uint16_t company_id;   // MESH_NET_COMPANY_ID, doubles as a packet filter
  uint16_t node_id;      // originating node, from its BT MAC
  uint8_t seq;           // originator's sequence number, wraps at 256
  uint8_t ttl;           // hops left; relayed only while > 0
  int16_t accel_mg[3];   // accel x/y/z, milli-g
  int16_t gyro_dps10[3]; // gyro x/y/z, deci-degrees/s
  int16_t rpy_dd[3];     // roll/pitch/yaw, deci-degrees
} mesh_pkt_t;

_Static_assert(sizeof(mesh_pkt_t) == 24,
               "mesh packet grew past the ADV budget");

#define RELAY_QUEUE_CAP 8
#define SEEN_TABLE_CAP 16
#define ADV_CYCLE_MS 150
#define SCAN_ITVL_MS 60
#define SCAN_WINDOW_MS 60

static uint16_t s_node_id;
static uint8_t s_next_seq;

static SemaphoreHandle_t s_lock;
static mesh_pkt_t s_own_packet;
static bool s_have_own_packet;

static mesh_pkt_t s_relay_queue[RELAY_QUEUE_CAP];
static int s_relay_head, s_relay_count;

// Per-originator last-seen sequence number, so a packet flooding back to its
// relayers (or to the node that originated it) is dropped instead of looping
// forever. Small linear-scan table: fine for a handful of mesh nodes.
static struct {
  uint16_t node_id;
  uint8_t last_seq;
  bool valid;
} s_seen[SEEN_TABLE_CAP];

static bool seen_mark_and_check(uint16_t node_id, uint8_t seq) {
  int free_slot = -1;
  for (int i = 0; i < SEEN_TABLE_CAP; i++) {
    if (!s_seen[i].valid) {
      if (free_slot < 0)
        free_slot = i;
      continue;
    }
    if (s_seen[i].node_id == node_id) {
      if (s_seen[i].last_seq == seq) {
        return true; // already processed this exact reading
      }
      s_seen[i].last_seq = seq;
      return false;
    }
  }
  int slot = (free_slot >= 0) ? free_slot : 0; // table full: evict slot 0
  s_seen[slot] =
      (typeof(s_seen[0])){.node_id = node_id, .last_seq = seq, .valid = true};
  return false;
}

static void relay_queue_push(const mesh_pkt_t *pkt) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_relay_count < RELAY_QUEUE_CAP) {
    int tail = (s_relay_head + s_relay_count) % RELAY_QUEUE_CAP;
    s_relay_queue[tail] = *pkt;
    s_relay_count++;
  } // else: queue full, drop — better than stalling own readings
  xSemaphoreGive(s_lock);
}

// Picks the next packet to transmit: pending relays first (keep the mesh
// moving), falling back to this node's own latest reading.
static bool next_broadcast_packet(mesh_pkt_t *out) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool got = false;
  if (s_relay_count > 0) {
    *out = s_relay_queue[s_relay_head];
    s_relay_head = (s_relay_head + 1) % RELAY_QUEUE_CAP;
    s_relay_count--;
    got = true;
  } else if (s_have_own_packet) {
    *out = s_own_packet;
    got = true;
  }
  xSemaphoreGive(s_lock);
  return got;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
  // Non-connectable broadcaster: nothing to react to per-event, the
  // broadcast task drives advertising cycles itself.
  return 0;
}

static void broadcast_one(const mesh_pkt_t *pkt) {
  struct ble_hs_adv_fields fields;
  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.mfg_data = (const uint8_t *)pkt;
  fields.mfg_data_len = sizeof(*pkt);

  uint8_t own_addr_type;
  if (ble_hs_id_infer_auto(0, &own_addr_type) != 0)
    return;

  ble_gap_adv_stop(); // no-op if nothing in flight

  if (ble_gap_adv_set_fields(&fields) != 0)
    return;

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(own_addr_type, NULL, ADV_CYCLE_MS, &adv_params,
                    gap_event_cb, NULL);
}

static void broadcast_task(void *arg) {
  mesh_pkt_t pkt;
  while (1) {
    if (next_broadcast_packet(&pkt)) {
      broadcast_one(&pkt);
    }
    vTaskDelay(pdMS_TO_TICKS(ADV_CYCLE_MS));
  }
}

static void handle_discovered_packet(const mesh_pkt_t *pkt) {
  if (seen_mark_and_check(pkt->node_id, pkt->seq)) {
    return; // already relayed/originated this one
  }

  ESP_LOGI(TAG,
           "node %04x seq=%u ttl=%u accel_mg=[%d %d %d] gyro_dps10=[%d %d %d] "
           "rpy_dd=[%d %d %d]",
           pkt->node_id, pkt->seq, pkt->ttl, pkt->accel_mg[0], pkt->accel_mg[1],
           pkt->accel_mg[2], pkt->gyro_dps10[0], pkt->gyro_dps10[1],
           pkt->gyro_dps10[2], pkt->rpy_dd[0], pkt->rpy_dd[1], pkt->rpy_dd[2]);

  if (pkt->ttl == 0) {
    return; // at the edge of its hop budget, don't forward further
  }
  mesh_pkt_t relay = *pkt;
  relay.ttl--;
  relay_queue_push(&relay);
}

static int scan_event_cb(struct ble_gap_event *event, void *arg) {
  if (event->type != BLE_GAP_EVENT_DISC)
    return 0;

  struct ble_hs_adv_fields fields;
  if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                              event->disc.length_data) != 0) {
    return 0;
  }
  if (!fields.mfg_data || fields.mfg_data_len != sizeof(mesh_pkt_t)) {
    return 0;
  }
  mesh_pkt_t pkt;
  memcpy(&pkt, fields.mfg_data, sizeof(pkt));
  if (pkt.company_id != MESH_NET_COMPANY_ID) {
    return 0;
  }
  handle_discovered_packet(&pkt);
  return 0;
}

static void start_scan(void) {
  uint8_t own_addr_type;
  if (ble_hs_id_infer_auto(0, &own_addr_type) != 0)
    return;

  struct ble_gap_disc_params disc_params = {0};
  disc_params.passive = 1;
  disc_params.filter_duplicates =
      0; // we need every packet, not just the first per address
  disc_params.itvl = SCAN_ITVL_MS / 0.625; // ADV/scan timing units are 0.625 ms
  disc_params.window = SCAN_WINDOW_MS / 0.625;

  int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                        scan_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
  }
}

static void on_sync(void) {
  ble_hs_util_ensure_addr(0);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  s_node_id = ((uint16_t)mac[4] << 8) | mac[5];
  ESP_LOGI(TAG, "mesh node id: %04x", s_node_id);

  start_scan();
  xTaskCreate(broadcast_task, "mesh_bcast", 4096, NULL, 5, NULL);
}

static void on_reset(int reason) {
  ESP_LOGW(TAG, "nimble host reset, reason=%d", reason);
}

static void host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t mesh_net_init(void) {
  s_lock = xSemaphoreCreateMutex();

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
    return err;

  err = nimble_port_init();
  if (err != ESP_OK)
    return err;

  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  nimble_port_freertos_init(host_task);
  return ESP_OK;
}

void mesh_net_publish(const lsm6ds3_data_t *imu, const orientation_t *orient) {
  mesh_pkt_t pkt = {
      .company_id = MESH_NET_COMPANY_ID,
      .node_id = s_node_id,
      .seq = s_next_seq++,
      .ttl = MESH_NET_TTL_MAX,
      .accel_mg =
          {
              (int16_t)(imu->accel_g.x * 1000.0f),
              (int16_t)(imu->accel_g.y * 1000.0f),
              (int16_t)(imu->accel_g.z * 1000.0f),
          },
      .gyro_dps10 =
          {
              (int16_t)(imu->gyro_dps.x * 10.0f),
              (int16_t)(imu->gyro_dps.y * 10.0f),
              (int16_t)(imu->gyro_dps.z * 10.0f),
          },
      .rpy_dd =
          {
              (int16_t)(orient->roll_deg * 10.0f),
              (int16_t)(orient->pitch_deg * 10.0f),
              (int16_t)(orient->yaw_deg * 10.0f),
          },
  };

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_own_packet = pkt;
  s_have_own_packet = true;
  xSemaphoreGive(s_lock);

  // A node has already "seen" its own reading, so if a neighbor relays it
  // back, seen_mark_and_check() drops it instead of re-queuing it forever.
  seen_mark_and_check(pkt.node_id, pkt.seq);
}

uint16_t mesh_net_node_id(void) { return s_node_id; }
