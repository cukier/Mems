#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "lsm6ds3.h"
#include "orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lightweight flood-mesh over BLE advertising: every node broadcasts its own
// IMU reading as manufacturer-specific data (non-connectable ADV) and, while
// scanning, relays other nodes' readings a few hops further with a
// decrementing TTL and a seen-cache to stop loops. No pairing/provisioning —
// any BLE scanner (e.g. nRF Connect) filtering on MESH_NET_COMPANY_ID sees
// every reachable node's data. See main/mesh_net.c for the packet format.

// Bluetooth SIG "company ID" 0xFFFF is reserved for testing/prototyping and
// must not ship in a real product, but it's the right choice for a private,
// non-commercial mesh like this one: no registration needed, and it doubles
// as a cheap filter for "is this one of our packets".
#define MESH_NET_COMPANY_ID 0xFFFF

// Hop budget: a fresh reading can be relayed this many times before nodes
// stop forwarding it further.
#define MESH_NET_TTL_MAX 3

// Brings up the NimBLE stack, starts continuous passive scanning (to receive
// and relay other nodes' packets), and starts the round-robin broadcast task
// (own reading + pending relays). Call once from app_main before the sensor
// loop starts publishing.
esp_err_t mesh_net_init(void);

// Call once per sensor sample (from the main loop) to hand the latest IMU +
// orientation reading to the mesh. Bumps the node's sequence number and
// queues the reading to go out on the next broadcast cycle.
void mesh_net_publish(const lsm6ds3_data_t *imu, const orientation_t *orient);

// This node's mesh address, derived from its Bluetooth MAC. Stable across
// reboots, useful for telling nodes apart in logs/scanner output.
uint16_t mesh_net_node_id(void);

#ifdef __cplusplus
}
#endif
