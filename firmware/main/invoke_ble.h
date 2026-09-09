#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "invoke_round.h" // round_q_t

#ifdef __cplusplus
extern "C" {
#endif

// BLE transport for the INVOKE Band — see docs/INVOKE_BLE_ESPECIFICACAO.md.
//
// Two layers on every band, at once:
//  - GATT / Nordic UART Service: the Web Bluetooth teacher app connects to one
//    band (its "proxy"). RX (app->proxy) carries the question-dispatch JSON;
//    TX (proxy->app) notifies each band's answer JSON.
//  - BLE 5 extended advertising: the proxy re-broadcasts the question (chunked)
//    over ext-adv; the other bands scan for it, run the round, and broadcast
//    their answer the same way; the proxy reassembles answers and forwards
//    them to the app over TX. Star topology, one classroom, no multi-hop.
//
// Legacy adv instance 0 stays connectable the whole time (flags + NUS UUID +
// name) so Chrome's picker always lists "INVOKE-xx"; instance 1 is the
// non-connectable extended instance that carries round chunks.

#define INVOKE_NUS_SERVICE_UUID_STR "6e400001-b5a3-f393-e0a9-e50e24dcca9e"

// Bring up NimBLE (peripheral + broadcaster + observer), the NUS GATT service,
// the two advertising instances, and the (duty-cycled) mesh scan. Call once
// from app_main.
esp_err_t invoke_ble_init(void);

// This band's number (1..64), from NVS (CONFIG_INVOKE_DEFAULT_BAND_NUM until
// provisioned). Drives the "INVOKE-xx" name and the "n" field of answers.
uint8_t invoke_band_number(void);

// The advertised BLE name, "INVOKE-xx".
const char *invoke_ble_name(void);

// Proxy path: re-broadcast a received question over ext-adv instance 1,
// chunked, for a fixed window. `json` is the raw question-dispatch JSON.
void invoke_ble_broadcast_question(const char *json, size_t len);

// Broadcast this band's answer over ext-adv instance 1 (1 chunk, jittered
// window). `letter` is 'A'..'D', or 0 for "no answer". If an app is connected
// to this band it is also notified directly over TX.
void invoke_ble_broadcast_answer(uint16_t rid, uint8_t band, char letter);

// Told by the round state machine: true from question received until the round
// returns to WAIT. Switches the mesh scan between a light idle duty cycle and
// continuous.
void invoke_ble_set_round_active(bool active);

// Main-loop hook: pops a question this band should run (received over GATT on
// the proxy, or reassembled from ext-adv on any other band). Returns false
// when none is pending. Keeps TFT drawing off the NimBLE host task.
bool invoke_ble_take_question(round_q_t *out);

#ifdef __cplusplus
}
#endif
