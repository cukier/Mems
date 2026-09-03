#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "invoke_ble.h"
#include "invoke_game.h"
#include "st7735.h"

static const char *TAG = "invoke_game";

// §3.3's timing/detection details the spec leaves unspecified are called
// out explicitly below rather than silently guessed at — this has never
// been tried against a real gesture, so treat the constants (especially
// GESTURE_THRESHOLD_G and which axis maps to which direction) as a first
// cut to tune once it's on an actual wrist.

#define CAPTURE_WINDOW_US (8 * 1000 * 1000) // spec §3.3: 8s capture window
#define ACK_WINDOW_US (2 * 1000 * 1000)     // spec §3.3: ~2s ack display

// Accel delta (g) from the CAPTURE-start baseline that counts as a
// deliberate flick rather than hand jitter. Untuned guess.
#define GESTURE_THRESHOLD_G 0.5f

typedef enum {
    GAME_IDLE,
    GAME_COUNTDOWN,
    GAME_CAPTURE,
    GAME_ACK,
} game_state_t;

static game_state_t s_state = GAME_IDLE;
static int64_t s_next_event_us;
static int s_countdown_remaining;
static lsm6ds3_axes_t s_capture_baseline;
static char s_captured_dir; // 0 = none yet this CAPTURE window

// --- TFT screens ---------------------------------------------------------

static void draw_centered(int16_t y, const char *s, uint16_t fg, uint8_t scale) {
    int w = (int)strlen(s) * 6 * scale;
    int x = (ST7735_WIDTH - w) / 2;
    if (x < 0) x = 0;
    st7735_draw_text((int16_t)x, y, s, fg, ST7735_BLACK, scale);
}

static void draw_countdown_screen(int remaining) {
    st7735_fill_screen(ST7735_BLACK);
    char band_line[16];
    snprintf(band_line, sizeof(band_line), "INVOKE-%02u", invoke_band_number());
    draw_centered(6, band_line, ST7735_GRAY, 1);
    char num[4];
    snprintf(num, sizeof(num), "%d", remaining);
    draw_centered(45, num, ST7735_YELLOW, 6);
}

static void draw_capture_screen(void) {
    st7735_fill_screen(ST7735_BLACK);
    draw_centered(2, "GO", ST7735_GREEN, 2);
    st7735_draw_text(56, 20, "U", ST7735_WHITE, ST7735_BLACK, 3);
    st7735_draw_text(6, 56, "L", ST7735_WHITE, ST7735_BLACK, 3);
    st7735_draw_text(106, 56, "R", ST7735_WHITE, ST7735_BLACK, 3);
    st7735_draw_text(56, 100, "D", ST7735_WHITE, ST7735_BLACK, 3);
}

static const char *dir_label(char dir) {
    switch (dir) {
        case 'u': return "UP";
        case 'd': return "DOWN";
        case 'l': return "LEFT";
        case 'r': return "RIGHT";
        default: return "NO ANSWER";
    }
}

static void draw_ack_screen(char dir) {
    st7735_fill_screen(ST7735_BLACK);
    draw_centered(50, dir_label(dir), dir ? ST7735_GREEN : ST7735_RED, dir ? 3 : 2);
}

// --- Gesture detection ----------------------------------------------------

// First axis (X then Y) to cross the threshold since CAPTURE started wins;
// sign gives the direction. Z is ignored (assumed to stay ~gravity — this
// is a wrist flick, not a flip).
static char detect_gesture(const lsm6ds3_data_t *imu) {
    float dx = imu->accel_g.x - s_capture_baseline.x;
    float dy = imu->accel_g.y - s_capture_baseline.y;

    if (fabsf(dx) >= fabsf(dy)) {
        if (fabsf(dx) >= GESTURE_THRESHOLD_G) return dx > 0 ? 'r' : 'l';
    } else {
        if (fabsf(dy) >= GESTURE_THRESHOLD_G) return dy > 0 ? 'u' : 'd';
    }
    return 0;
}

// --- State machine ---------------------------------------------------------

static void enter_capture(const lsm6ds3_data_t *imu) {
    s_capture_baseline = imu->accel_g;
    s_captured_dir = 0;
    s_state = GAME_CAPTURE;
    s_next_event_us = esp_timer_get_time() + CAPTURE_WINDOW_US;
    ESP_LOGI(TAG, "-> CAPTURE (8s window open)");
    draw_capture_screen();
}

static void enter_ack(char dir) {
    s_captured_dir = dir;
    s_state = GAME_ACK;
    s_next_event_us = esp_timer_get_time() + ACK_WINDOW_US;
    ESP_LOGI(TAG, "-> ACK: %s", dir ? (char[]){dir, '\0'} : "(no gesture)");
    draw_ack_screen(dir);
}

void invoke_game_init(void) {
    s_state = GAME_IDLE;
}

void invoke_game_on_question(uint8_t cd) {
    if (s_state != GAME_IDLE) {
        ESP_LOGI(TAG, "question ignored, one already in progress");
        return; // a question is already running; first one wins
    }

    ESP_LOGI(TAG, "-> COUNTDOWN: cd=%u", cd);
    s_countdown_remaining = cd;
    s_state = GAME_COUNTDOWN;
    s_next_event_us = esp_timer_get_time() + 1000000;
    draw_countdown_screen(s_countdown_remaining);
}

void invoke_game_tick(const lsm6ds3_data_t *imu) {
    int64_t now = esp_timer_get_time();

    switch (s_state) {
        case GAME_IDLE:
            return;

        case GAME_COUNTDOWN:
            if (now < s_next_event_us) return;
            s_countdown_remaining--;
            if (s_countdown_remaining <= 0) {
                enter_capture(imu);
            } else {
                ESP_LOGI(TAG, "countdown: %d", s_countdown_remaining);
                draw_countdown_screen(s_countdown_remaining);
                s_next_event_us += 1000000;
            }
            return;

        case GAME_CAPTURE: {
            char dir = detect_gesture(imu);
            if (dir) {
                invoke_mesh_send_gesture(dir);
                enter_ack(dir);
                return;
            }
            if (now >= s_next_event_us) {
                enter_ack(0); // timed out with no gesture
            }
            return;
        }

        case GAME_ACK:
            if (now >= s_next_event_us) {
                ESP_LOGI(TAG, "-> IDLE");
                s_state = GAME_IDLE;
            }
            return;
    }
}

bool invoke_game_is_idle(void) {
    return s_state == GAME_IDLE;
}
