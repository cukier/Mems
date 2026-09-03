#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// BLE transport for the INVOKE Band protocol — see
// docs/INVOKE_BLE_ESPECIFICACAO.md for the full spec this implements.
//
// Two layers, both required to run at once on every node:
//  - GATT: a Nordic UART Service (NUS) the Web Bluetooth app connects to on
//    whichever node it picks as its "proxy". RX (app->node) carries JSON
//    question commands; TX (node->app) notifies JSON gesture reports.
//  - Mesh: a flood network carried in BLE advertising manufacturer data
//    (company ID 0xFFFF). Every node relays what it hears (bounded by a hop
//    count) whether or not it currently has a GATT connection, so a
//    question/gesture reaches the whole class regardless of which node the
//    app is attached to.
//
// This module owns NimBLE end to end (host init, GATT service, advertising,
// scanning) — it replaces mesh_net.c's IMU-telemetry flood-mesh, which used
// the same BLE company ID for an unrelated wire format and can't coexist
// with this protocol on the same devices.
//
// Out of scope here (left for whoever wires up the gesture-capture side):
// the IDLE/COUNTDOWN/CAPTURE/ACK state machine, MPU6050 gesture detection,
// and OLED rendering (spec §3.3). invoke_mesh_send_gesture() is the hook
// that code calls once it captures a gesture.

// Brings up NimBLE (peripheral + broadcaster + observer roles), the NUS
// GATT service, normal advertising (flags + NUS UUID / name), and
// continuous passive scanning for mesh traffic. Call once from app_main.
esp_err_t invoke_ble_init(void);

// This node's band number (1..64), persisted in NVS. Falls back to
// CONFIG_INVOKE_DEFAULT_BAND_NUM the first time a node boots with none set.
// Drives the BLE name ("INVOKE-xx") and the "b" field of gesture reports.
uint8_t invoke_band_number(void);

// Originates a gesture (G) mesh message for this node's own band and sends
// it immediately (no jitter — jitter only applies when *relaying* someone
// else's message, per spec §3.2). dir must be one of 'u'/'d'/'l'/'r'.
void invoke_mesh_send_gesture(char dir);

#ifdef __cplusplus
}
#endif
