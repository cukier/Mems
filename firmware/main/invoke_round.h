#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lsm6ds3.h"
#include "orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

// The INVOKE Band round state machine — see docs/INVOKE_BLE_ESPECIFICACAO.md.
//
//   WAIT ──(question)──> COUNTDOWN ──(cd s)──> ANSWER ──(to s)──> RESULT ──(3s)──> WAIT
//
// WAIT      shows the NUS UUID + this band's name and idles.
// COUNTDOWN shows the statement + the 4 options and a shrinking counter.
// ANSWER    same screen; the option the wrist is tilted toward is highlighted
//           live (up=A, down=B, left=C, right=D; centre = none). The option
//           held when the timer expires is the answer.
// RESULT    shows the answer for ~3s and broadcasts it (invoke_ble).

#define INVOKE_STMT_MAX 192
#define INVOKE_OPT_MAX 40

typedef struct {
  uint16_t rid; // round id (from the teacher app)
  uint8_t cd;   // countdown seconds before answering opens
  uint8_t to;   // answer window seconds
  char stmt[INVOKE_STMT_MAX];
  char opt[4][INVOKE_OPT_MAX]; // A, B, C, D — already ASCII-folded for the TFT
} round_q_t;

// Call once from app_main, after st7735_init().
void invoke_round_init(void);

// Called by invoke_ble.c when a question arrives (GATT RX on the proxy, or an
// ext-adv reassembly on any other band). Ignored if a round is already
// running. Starts COUNTDOWN.
void invoke_round_on_question(const round_q_t *q);

// Call once per IMU sample (~20 Hz) from the main loop. Drives all timing and,
// during ANSWER, the live tilt -> option mapping and redraw.
void invoke_round_tick(const orientation_t *ori, const lsm6ds3_data_t *imu);

// 'A'..'D' for the answer this band last locked in, or 0 for "no answer".
char invoke_round_last_answer(void);

#ifdef __cplusplus
}
#endif
