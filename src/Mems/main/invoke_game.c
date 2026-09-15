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

#define DEFAULT_ANSWER_S 8              // spec §3.3 default; the Q's "to" overrides
#define MIN_ANSWER_S 1
#define MAX_ANSWER_S 60
#define ACK_WINDOW_US (2 * 1000 * 1000) // spec §3.3: ~2s ack display

// Accel delta (g) from the CAPTURE-start baseline that counts as a
// deliberate flick rather than hand jitter. Untuned guess.
#define GESTURE_THRESHOLD_G 0.5f

// Consecutive 200ms ticks (see app_main's poll loop) the same direction must
// stay over threshold before it commits as the gesture. A single-tick spike
// (bench bump, cable tug, the board simply not resting flat) reads as a
// real-looking delta for one sample and then relaxes back; a deliberate
// flick holds. Observed on the bench: isolated one-tick triggers with no one
// touching the board — this is the fix for that, not a threshold change.
#define GESTURE_DEBOUNCE_TICKS 2

// The answer screen previews which answer the band is currently tilted toward.
// Smaller than GESTURE_THRESHOLD_G so the highlight tracks the wrist well
// before a tilt would count as the committed gesture.
#define TILT_DEADZONE_G 0.18f

// Answer screen layout (128x128).
#define ANS_NUM_Y 20  // big seconds-left number
#define GRID_Y 56     // arrow grid starts here

typedef enum {
    GAME_IDLE,
    GAME_ANSWER,  // single phase: "VÁ!" + seconds counting down + gesture capture
    GAME_ACK,
} game_state_t;

static game_state_t s_state = GAME_IDLE;
static int64_t s_next_event_us;   // deadline for the current phase
static int64_t s_sec_tick_us;     // next 1s number decrement in GAME_ANSWER
static int s_secs_left;            // seconds shown on the "VÁ!" screen
static bool s_answer_pending;      // GAME_ANSWER just entered; first tick captures the baseline
static char s_aim_dir;             // answer the wrist is tilted toward (live preview)
static char s_pending_dir;         // direction over threshold on the last tick (debounce)
static int s_pending_ticks;        // consecutive ticks s_pending_dir has held
static lsm6ds3_axes_t s_answer_baseline;
static uint8_t s_answer_secs = DEFAULT_ANSWER_S; // window for the current round (Q "to")
static char s_idle_addr[18]; // BLE address the standby screen currently shows ("" = needs redraw)

// --- TFT screens ---------------------------------------------------------

static void draw_centered(int16_t y, const char *s, uint16_t fg, uint8_t scale) {
    int w = (int)strlen(s) * 6 * scale;
    int x = (ST7735_WIDTH - w) / 2;
    if (x < 0) x = 0;
    st7735_draw_text((int16_t)x, y, s, fg, ST7735_BLACK, scale);
}

// Standby screen: band number + this boot's BLE address. Shown whenever no
// question is running (spec §3.3 IDLE). The address line reads "..." until the
// NimBLE host has synced; the IDLE tick redraws once it's known.
static void draw_idle_screen(void) {
    st7735_fill_screen(ST7735_BLACK);
    char line[24];

    draw_centered(8, "PRONTA", ST7735_GREEN, 1);

    snprintf(line, sizeof(line), "INVOKE-%02u", invoke_band_number());
    draw_centered(30, line, ST7735_WHITE, 2);

    snprintf(line, sizeof(line), "Pulseira %u", invoke_band_number());
    draw_centered(54, line, ST7735_GRAY, 1);

    draw_centered(74, "AGUARDANDO", ST7735_GRAY, 1);
    draw_centered(84, "PERGUNTA", ST7735_GRAY, 1);

    const char *addr = invoke_ble_addr_str();
    draw_centered(106, addr[0] ? addr : "...", ST7735_GRAY, 1);
    draw_centered(118, "built " __TIME__, ST7735_GRAY, 1);
}

// Fixed direction<->letter map, matching the capture-screen layout and
// docs/INVOKE_BLE_ESPECIFICACAO.md §2.1: up=A, left=B, right=C, down=D.
static char dir_to_letter(char dir) {
    switch (dir) {
        case 'u': return 'A';
        case 'l': return 'B';
        case 'r': return 'C';
        case 'd': return 'D';
        default: return 0;
    }
}

// Small filled arrow (~24x24) centred at (cx, cy), pointing dir ('u'/'d'/'l'/'r').
static void draw_arrow(char dir, int16_t cx, int16_t cy, uint16_t color) {
    const int L = 12;   // half-extent along the pointing axis
    const int SW = 5;   // stem half-width
    const int HL = 9;   // head length
    const int HW = 10;  // head half-width at its base
    switch (dir) {
        case 'u':
            st7735_fill_rect(cx - SW, cy - L + HL, 2 * SW + 1, L - HL, color);
            for (int i = 0; i < HL; i++) {
                int hw = HW - (HW * i) / HL;
                st7735_fill_rect(cx - hw, cy - L + i, 2 * hw + 1, 1, color);
            }
            break;
        case 'd':
            st7735_fill_rect(cx - SW, cy - L, 2 * SW + 1, L - HL, color);
            for (int i = 0; i < HL; i++) {
                int hw = HW - (HW * i) / HL;
                st7735_fill_rect(cx - hw, cy + L - i, 2 * hw + 1, 1, color);
            }
            break;
        case 'l':
            st7735_fill_rect(cx - L + HL, cy - SW, L - HL, 2 * SW + 1, color);
            for (int i = 0; i < HL; i++) {
                int hw = HW - (HW * i) / HL;
                st7735_fill_rect(cx - L + i, cy - hw, 1, 2 * hw + 1, color);
            }
            break;
        case 'r':
            st7735_fill_rect(cx - L, cy - SW, L - HL, 2 * SW + 1, color);
            for (int i = 0; i < HL; i++) {
                int hw = HW - (HW * i) / HL;
                st7735_fill_rect(cx + L - i, cy - hw, 1, 2 * hw + 1, color);
            }
            break;
    }
}

// The four answer arrows in the fixed layout, the one matching `active`
// highlighted. Clears only its own region so it can be redrawn every tick
// without flicker (the countdown number and title stay put).
static void draw_answer_grid(char active) {
    static const struct {
        char dir;
        int16_t cx, cy;
    } cells[4] = {
        {'u', 64, 70}, {'l', 26, 95}, {'r', 102, 95}, {'d', 64, 114},
    };
    st7735_fill_rect(0, GRID_Y, ST7735_WIDTH, ST7735_HEIGHT - GRID_Y, ST7735_BLACK);
    for (int i = 0; i < 4; i++) {
        bool on = (active == cells[i].dir);
        uint16_t col = on ? ST7735_YELLOW : ST7735_GRAY;
        draw_arrow(cells[i].dir, cells[i].cx, cells[i].cy, col);
        char lbl[2] = {dir_to_letter(cells[i].dir), '\0'};
        st7735_draw_text(cells[i].cx - 6, cells[i].cy - 8, lbl,
                         on ? ST7735_BLACK : ST7735_WHITE, col, 2);
    }
}

// Redraws just the seconds-left number on the answer screen (own region, so it
// can tick every second without disturbing "VÁ!" or the arrow grid).
static void draw_answer_secs(int remaining) {
    st7735_fill_rect(0, ANS_NUM_Y, ST7735_WIDTH, GRID_Y - ANS_NUM_Y, ST7735_BLACK);
    char n[4];
    snprintf(n, sizeof(n), "%d", remaining);
    draw_centered(ANS_NUM_Y, n, ST7735_YELLOW, 4);
}

// The whole round on one screen: "VÁ!" + the chosen answer time counting down
// + the A/B/C/D arrow grid, which highlights the answer the wrist is aimed at,
// live, until a flick commits it (or the count hits zero).
static void draw_answer_screen(void) {
    st7735_fill_screen(ST7735_BLACK);
    draw_centered(2, "VA!", ST7735_GREEN, 2);
    draw_answer_secs(s_secs_left);
    draw_answer_grid(s_aim_dir);
}

static void draw_ack_screen(char dir) {
    st7735_fill_screen(ST7735_BLACK);
    char letter = dir_to_letter(dir);
    if (letter) {
        char s[2] = {letter, '\0'};
        draw_centered(36, s, ST7735_GREEN, 7);
    } else {
        draw_centered(50, "NO ANSWER", ST7735_RED, 2);
    }
}

// --- Gesture detection ----------------------------------------------------

// First axis (X then Y) to cross the threshold since the answer window opened
// wins; sign gives the direction. Z is ignored (assumed to stay ~gravity —
// this is a wrist flick, not a flip).
static char detect_gesture(const lsm6ds3_data_t *imu) {
    float dx = imu->accel_g.x - s_answer_baseline.x;
    float dy = imu->accel_g.y - s_answer_baseline.y;

    if (fabsf(dx) >= fabsf(dy)) {
        if (fabsf(dx) >= GESTURE_THRESHOLD_G) {
            ESP_LOGI(TAG, "gesture trigger: dx=%.2f dy=%.2f baseline=(%.2f,%.2f)",
                     dx, dy, s_answer_baseline.x, s_answer_baseline.y);
            return dx > 0 ? 'r' : 'l';
        }
    } else {
        if (fabsf(dy) >= GESTURE_THRESHOLD_G) {
            ESP_LOGI(TAG, "gesture trigger: dx=%.2f dy=%.2f baseline=(%.2f,%.2f)",
                     dx, dy, s_answer_baseline.x, s_answer_baseline.y);
            return dy > 0 ? 'u' : 'd';
        }
    }
    return 0;
}

// Which answer the band is currently tilted toward, from a flat/neutral
// reference (no captured baseline) with a dead zone. Same axis convention as
// detect_gesture, so the live preview points where a gesture would land.
static char tilt_direction(const lsm6ds3_data_t *imu) {
    float ax = imu->accel_g.x;
    float ay = imu->accel_g.y;
    if (fabsf(ax) < TILT_DEADZONE_G && fabsf(ay) < TILT_DEADZONE_G) return 0;
    if (fabsf(ax) >= fabsf(ay)) return ax > 0 ? 'r' : 'l';
    return ay > 0 ? 'u' : 'd';
}

// --- State machine ---------------------------------------------------------

// First tick of GAME_ANSWER: captures the IMU baseline (invoke_game_on_question
// has no sample) and opens the window.
static void enter_answer(const lsm6ds3_data_t *imu) {
    s_answer_baseline = imu->accel_g;
    s_aim_dir = 0;
    s_pending_dir = 0;
    s_pending_ticks = 0;
    s_secs_left = s_answer_secs;
    s_answer_pending = false;
    int64_t now = esp_timer_get_time();
    s_next_event_us = now + (int64_t)s_answer_secs * 1000000;
    s_sec_tick_us = now + 1000000;
    ESP_LOGI(TAG, "-> ANSWER window open (%us)", s_answer_secs);
    draw_answer_screen();
}

static void enter_ack(char dir) {
    s_state = GAME_ACK;
    s_next_event_us = esp_timer_get_time() + ACK_WINDOW_US;
    ESP_LOGI(TAG, "-> ACK: %s", dir ? (char[]){dir, '\0'} : "(no gesture)");
    draw_ack_screen(dir);
}

void invoke_game_init(void) {
    s_state = GAME_IDLE;
    s_idle_addr[0] = '\0';
    draw_idle_screen(); // shows "..." for the address; the IDLE tick redraws
                        // once the NimBLE host has synced
}

void invoke_game_on_question(uint8_t answer_secs) {
    if (s_state != GAME_IDLE) {
        ESP_LOGI(TAG, "question ignored, one already in progress");
        return; // a question is already running; first one wins
    }

    s_answer_secs = answer_secs;
    if (s_answer_secs < MIN_ANSWER_S) s_answer_secs = MIN_ANSWER_S;
    if (s_answer_secs > MAX_ANSWER_S) s_answer_secs = MAX_ANSWER_S;

    ESP_LOGI(TAG, "-> ANSWER (%us)", s_answer_secs);
    s_state = GAME_ANSWER;
    s_answer_pending = true; // next tick captures the IMU baseline and opens the window
}

void invoke_game_tick(const lsm6ds3_data_t *imu) {
    int64_t now = esp_timer_get_time();

    switch (s_state) {
        case GAME_IDLE: {
            // Redraw the standby screen once the BLE address is known (it isn't
            // yet when invoke_game_init() runs), or after it changed.
            const char *addr = invoke_ble_addr_str();
            if (strcmp(addr, s_idle_addr) != 0) {
                snprintf(s_idle_addr, sizeof(s_idle_addr), "%s", addr);
                draw_idle_screen();
            }
            return;
        }

        case GAME_ANSWER: {
            if (s_answer_pending) {
                enter_answer(imu); // first tick: capture baseline, open the window
                return;
            }
            char dir = detect_gesture(imu);
            if (dir && dir == s_pending_dir) {
                s_pending_ticks++;
            } else {
                s_pending_dir = dir;
                s_pending_ticks = dir ? 1 : 0;
            }
            if (dir && s_pending_ticks >= GESTURE_DEBOUNCE_TICKS) {
                invoke_mesh_send_gesture(dir);
                enter_ack(dir);
                return;
            }
            // Live aim preview: highlight the arrow the wrist points at.
            char aim = tilt_direction(imu);
            if (aim != s_aim_dir) {
                s_aim_dir = aim;
                draw_answer_grid(aim);
            }
            // The chosen answer time counting down on screen.
            if (now >= s_sec_tick_us && s_secs_left > 0) {
                s_secs_left--;
                draw_answer_secs(s_secs_left);
                s_sec_tick_us += 1000000;
            }
            if (now >= s_next_event_us) {
                enter_ack(0); // window closed with no gesture
            }
            return;
        }

        case GAME_ACK:
            if (now >= s_next_event_us) {
                ESP_LOGI(TAG, "-> IDLE");
                s_state = GAME_IDLE;
                s_idle_addr[0] = '\0'; // ACK screen is up — force a standby redraw
            }
            return;
    }
}

bool invoke_game_is_idle(void) {
    return s_state == GAME_IDLE;
}
