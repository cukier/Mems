#include "invoke_round.h"

#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "invoke_ble.h"
#include "screens.h"

static const char *TAG = "invoke_round";

// Tilt -> option. Absolute (gravity-referenced) roll/pitch from orientation.c.
// Past THRESH in the dominant axis selects; inside DEADZONE clears to "none";
// in between, the current selection is held (hysteresis). Tune on a wrist.
#define TILT_THRESH_DEG 25.0f
#define TILT_DEADZONE_DEG 12.0f
#define RESULT_MS 3000

typedef enum {
  ST_WAIT,
  ST_COUNTDOWN,
  ST_ANSWER,
  ST_RESULT,
} round_state_t;

static round_state_t s_state = ST_WAIT;
static round_q_t s_q;
static int64_t s_phase_end_us;
static int64_t s_sec_tick_us;
static int s_count_left;   // seconds remaining in COUNTDOWN
static char s_selected;    // live tilt selection, 'A'..'D' or 0
static char s_locked;      // answer latched at ANSWER end
static char s_last_answer; // exposed via invoke_round_last_answer()

static void show_wait(void) {
  screens_wait(INVOKE_NUS_SERVICE_UUID_STR, invoke_ble_name());
}

void invoke_round_init(void) {
  s_state = ST_WAIT;
  s_selected = 0;
  s_locked = 0;
  s_last_answer = 0;
  show_wait();
}

void invoke_round_on_question(const round_q_t *q) {
  if (s_state != ST_WAIT) {
    ESP_LOGI(TAG, "question ignored (round in progress)");
    return;
  }
  s_q = *q;
  if (s_q.to == 0) {
    s_q.to = 10;
  }
  s_count_left = s_q.cd > 0 ? s_q.cd : 1;
  s_selected = 0;
  s_locked = 0;
  s_state = ST_COUNTDOWN;

  int64_t now = esp_timer_get_time();
  s_phase_end_us = now + 1000000;
  s_sec_tick_us = now + 1000000;

  ESP_LOGI(TAG, "-> COUNTDOWN rid=%u cd=%u to=%u", s_q.rid, s_q.cd, s_q.to);
  screens_question(&s_q, 0, s_count_left);
}

static char tilt_to_letter(const orientation_t *ori) {
  float r = ori->roll_deg;
  float p = ori->pitch_deg;
  float ar = fabsf(r), ap = fabsf(p);

  if (ar < TILT_DEADZONE_DEG && ap < TILT_DEADZONE_DEG) {
    return 0; // centred
  }
  if (ap >= ar) {
    if (p >= TILT_THRESH_DEG) return 'A';  // tilt up
    if (p <= -TILT_THRESH_DEG) return 'B'; // tilt down
  } else {
    if (r <= -TILT_THRESH_DEG) return 'C'; // tilt left
    if (r >= TILT_THRESH_DEG) return 'D';  // tilt right
  }
  return s_selected; // in the hysteresis band: hold
}

static int secs_left(int64_t now) {
  int64_t d = s_phase_end_us - now;
  if (d < 0) {
    d = 0;
  }
  return (int)((d + 999999) / 1000000);
}

static void enter_answer(int64_t now) {
  s_state = ST_ANSWER;
  s_selected = 0;
  s_phase_end_us = now + (int64_t)s_q.to * 1000000;
  s_sec_tick_us = now + 1000000;
  ESP_LOGI(TAG, "-> ANSWER (%us)", s_q.to);
  screens_question(&s_q, 0, s_q.to);
}

static void enter_result(int64_t now) {
  s_locked = s_selected;
  s_last_answer = s_locked;
  s_state = ST_RESULT;
  s_phase_end_us = now + RESULT_MS * 1000;

  const char *txt = (s_locked >= 'A' && s_locked <= 'D')
                        ? s_q.opt[s_locked - 'A']
                        : "";
  ESP_LOGI(TAG, "-> RESULT: %c", s_locked ? s_locked : '-');
  screens_result(s_locked, txt);
  invoke_ble_broadcast_answer(s_q.rid, invoke_band_number(), s_locked);
}

void invoke_round_tick(const orientation_t *ori, const lsm6ds3_data_t *imu) {
  (void)imu;
  int64_t now = esp_timer_get_time();

  switch (s_state) {
  case ST_WAIT:
    return;

  case ST_COUNTDOWN:
    if (now < s_phase_end_us) {
      return;
    }
    s_count_left--;
    s_phase_end_us += 1000000;
    if (s_count_left <= 0) {
      enter_answer(now);
    } else {
      screens_question_timebar(s_count_left, s_q.cd > 0 ? s_q.cd : 1);
    }
    return;

  case ST_ANSWER: {
    char sel = tilt_to_letter(ori);
    if (sel != s_selected) {
      s_selected = sel;
      screens_question_select(sel);
    }
    if (now >= s_sec_tick_us) {
      s_sec_tick_us += 1000000;
      screens_question_timebar(secs_left(now), s_q.to);
    }
    if (now >= s_phase_end_us) {
      enter_result(now);
    }
    return;
  }

  case ST_RESULT:
    if (now >= s_phase_end_us) {
      s_state = ST_WAIT;
      ESP_LOGI(TAG, "-> WAIT");
      show_wait();
    }
    return;
  }
}

char invoke_round_last_answer(void) { return s_last_answer; }
