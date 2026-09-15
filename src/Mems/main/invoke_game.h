#pragma once

#include "lsm6ds3.h"

#ifdef __cplusplus
extern "C" {
#endif

// The gesture-capture state machine from docs/INVOKE_BLE_ESPECIFICACAO.md
// §3.3: IDLE -> COUNTDOWN -> CAPTURE -> ACK -> IDLE, driven by incoming
// question (Q) mesh messages and this node's own IMU. Renders its own
// screens on the ST7735 TFT this board actually has (the spec assumes an
// OLED — same role, different part, since this project's test hardware
// never had one).
//
// Deliberately out of scope, same as the rest of this firmware: the app
// side and any real gesture-detection tuning against a physical wristband
// — see invoke_game.c's top comment for what's a placeholder here.

// Call once from app_main, after st7735_init().
void invoke_game_init(void);

// Called by invoke_ble.c when a Q mesh message names this node's band in its
// bitmap. `answer_secs` is the answer window in seconds (the app's "to" field,
// spec §2.1) — the band shows the question and captures the gesture over the
// whole window. `statement` is the question text for the screen (the app's
// "s" field); it never travels over the compact mesh frame, so mesh-relayed
// bands always pass "" here — only a band directly GATT-connected to the app
// has the real text. NULL is treated the same as "". No-op if a question is
// already in progress (first Q wins, same as the mesh layer's dedupe intent).
void invoke_game_on_question(uint8_t answer_secs, const char *statement);

// Call once per IMU sample from the main sensor loop (main.c already reads
// at ~5Hz, which is what CAPTURE's gesture-threshold check runs at). Drives
// every state's timing and, during CAPTURE, evaluates the incoming sample
// for a gesture.
void invoke_game_tick(const lsm6ds3_data_t *imu);

// False whenever a question is in progress (COUNTDOWN/CAPTURE/ACK) and the
// game screens own the TFT — main.c's regular IMU dashboard draw calls
// should be skipped while this is false, to avoid fighting over the screen.
bool invoke_game_is_idle(void);

#ifdef __cplusplus
}
#endif
