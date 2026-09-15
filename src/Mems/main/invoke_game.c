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
// TILT_DEADZONE_G and which axis maps to which direction) as a first cut
// to tune once it's on an actual wrist.
//
// The answer is *not* a flick detected mid-window: the student tilts their
// wrist toward an option, sees the live preview on screen the whole time,
// and whatever direction they're holding when the answer window closes is
// what gets sent. This also means the full "to" duration always elapses —
// there is nothing that can end the round early.

#define DEFAULT_ANSWER_S 8              // spec §3.3 default; the Q's "to" overrides
#define MIN_ANSWER_S 1
#define MAX_ANSWER_S 60
#define ACK_WINDOW_US (2 * 1000 * 1000) // spec §3.3: ~2s ack display

// Accel delta (g) from flat, on either axis, that counts as "tilted toward
// that answer" rather than the band just resting/jittering. Untuned guess.
#define TILT_DEADZONE_G 0.18f

// Answer screen layout (128x128): statement (up to 3 lines, scale 1) → the
// countdown number → the arrow grid.
#define STMT_Y0 2
#define STMT_LINE_H 8
#define STMT_MAX_LINES 3
#define NUM_Y (STMT_Y0 + STMT_MAX_LINES * STMT_LINE_H + 2)
#define NUM_H 16
#define GRID_Y 56 // arrow grid starts here

#define MAX_STATEMENT_LEN 48

typedef enum {
    GAME_IDLE,
    GAME_ANSWER,  // single phase: statement + seconds counting down + gesture capture
    GAME_ACK,
} game_state_t;

static game_state_t s_state = GAME_IDLE;
static int64_t s_next_event_us;   // deadline for the current phase
static int64_t s_sec_tick_us;     // next 1s number decrement in GAME_ANSWER
static int s_secs_left;            // seconds shown on the answer screen
static bool s_answer_pending;      // GAME_ANSWER just entered; first tick captures the baseline
static char s_aim_dir;             // answer the wrist is tilted toward — also
                                    // what gets sent when the window closes
static uint8_t s_answer_secs = DEFAULT_ANSWER_S; // window for the current round (Q "to")
static char s_statement[MAX_STATEMENT_LEN + 1];  // current round's question text ("" = none)
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

    draw_centered(8, "PRONTA", ST7735_AMBER, 1);

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
// `grow` grows (or, negative, shrinks) every dimension by the same amount —
// L and HL move together so the stem segment (L-HL) stays constant — which
// is what draw_answer_grid uses to fake a glow (a dim, grown copy behind the
// full-size bright one) and a hollow outline (a shrunk black copy on top of
// a full-size dim one) without the driver needing real blur/stroke support.
// Only called with |grow| <= 2 here, so no clamping against degenerate sizes.
static void draw_arrow(char dir, int16_t cx, int16_t cy, uint16_t color, int grow) {
    const int L = 12 + grow;   // half-extent along the pointing axis
    const int SW = 5 + grow;   // stem half-width
    const int HL = 9 + grow;   // head length
    const int HW = 10 + grow;  // head half-width at its base
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
// highlighted amber-glowing (a dim grown copy behind a bright full-size one);
// the other three are hollow amber outlines (a dim full-size copy with a
// black shrunk one punched on top). Clears only its own region so it can be
// redrawn every tick without flicker (the statement and countdown stay put).
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
        uint16_t letter_fg, letter_bg;
        if (on) {
            draw_arrow(cells[i].dir, cells[i].cx, cells[i].cy, ST7735_AMBER_DIM, 2);
            draw_arrow(cells[i].dir, cells[i].cx, cells[i].cy, ST7735_AMBER, 0);
            letter_fg = ST7735_BLACK;
            letter_bg = ST7735_AMBER;
        } else {
            draw_arrow(cells[i].dir, cells[i].cx, cells[i].cy, ST7735_AMBER_DIM, 0);
            draw_arrow(cells[i].dir, cells[i].cx, cells[i].cy, ST7735_BLACK, -2);
            letter_fg = ST7735_AMBER_DIM;
            letter_bg = ST7735_BLACK;
        }
        char lbl[2] = {dir_to_letter(cells[i].dir), '\0'};
        st7735_draw_text(cells[i].cx - 6, cells[i].cy - 8, lbl, letter_fg, letter_bg, 2);
    }
}

// Greedy word-wrap into up to STMT_MAX_LINES centered lines at scale 1 — the
// only text on this screen that isn't a single short fixed string, so it's
// the only one that needs it. A statement too long to fit is silently
// truncated to what does; there's no smaller scale and no scrolling.
static void draw_statement(const char *stmt) {
    if (!stmt || !stmt[0]) return;
    const int max_chars = ST7735_WIDTH / 6; // st7735's per-char advance at scale 1 (5px glyph + 1px gap)
    char line[32];
    int line_len = 0;
    int lines_drawn = 0;
    const char *word = stmt;
    while (*word && lines_drawn < STMT_MAX_LINES) {
        const char *sp = strchr(word, ' ');
        int wlen = sp ? (int)(sp - word) : (int)strlen(word);
        if (wlen > max_chars) wlen = max_chars; // one word alone is a whole line
        bool fits = line_len == 0 || line_len + 1 + wlen <= max_chars;
        if (!fits) {
            draw_centered(STMT_Y0 + lines_drawn * STMT_LINE_H, line, ST7735_WHITE, 1);
            lines_drawn++;
            line_len = 0;
            if (lines_drawn >= STMT_MAX_LINES) break;
        }
        if (line_len > 0 && line_len < (int)sizeof(line) - 1) line[line_len++] = ' ';
        int copy = wlen;
        if (line_len + copy > (int)sizeof(line) - 1) copy = (int)sizeof(line) - 1 - line_len;
        memcpy(line + line_len, word, (size_t)copy);
        line_len += copy;
        line[line_len] = '\0';
        word += wlen;
        while (*word == ' ') word++;
    }
    if (lines_drawn < STMT_MAX_LINES && line_len > 0) {
        draw_centered(STMT_Y0 + lines_drawn * STMT_LINE_H, line, ST7735_WHITE, 1);
    }
}

// Redraws just the seconds-left number on the answer screen (own region, so it
// can tick every second without disturbing the statement or the arrow grid).
static void draw_answer_secs(int remaining) {
    st7735_fill_rect(0, NUM_Y, ST7735_WIDTH, NUM_H, ST7735_BLACK);
    char n[4];
    snprintf(n, sizeof(n), "%d", remaining);
    draw_centered(NUM_Y, n, ST7735_AMBER, 2);
}

// The whole round on one screen: the question statement, the chosen answer
// time counting down, and the A/B/C/D arrow grid — which glows on whichever
// answer the wrist is currently aimed at, live, for the entire window (see
// the file header comment: there's no flick that ends it early).
static void draw_answer_screen(void) {
    st7735_fill_screen(ST7735_BLACK);
    draw_statement(s_statement);
    draw_answer_secs(s_secs_left);
    draw_answer_grid(s_aim_dir);
}

// Same visual language as the answer screen: the statement stays up, and the
// grid reappears with only the registered direction (if any) glowing — a
// held position, not a fresh choice, so the other three are just the resting
// hollow outline instead of disappearing.
static void draw_ack_screen(char dir) {
    st7735_fill_screen(ST7735_BLACK);
    draw_statement(s_statement);
    draw_centered(NUM_Y, dir ? "RESPOSTA" : "SEM RESPOSTA", dir ? ST7735_AMBER : ST7735_RED, 1);
    draw_answer_grid(dir);
}

// --- Gesture detection ----------------------------------------------------

// Which answer the band is currently tilted toward, from a flat/neutral
// (gravity-on-Z) reference, with a dead zone. This is the only detector:
// the wrist's position at the moment the window closes is the answer, so
// there is no separate flick/threshold check to land on Z.
static char tilt_direction(const lsm6ds3_data_t *imu) {
    float ax = imu->accel_g.x;
    float ay = imu->accel_g.y;
    if (fabsf(ax) < TILT_DEADZONE_G && fabsf(ay) < TILT_DEADZONE_G) return 0;
    if (fabsf(ax) >= fabsf(ay)) return ax > 0 ? 'r' : 'l';
    return ay > 0 ? 'u' : 'd';
}

// --- State machine ---------------------------------------------------------

// First tick of GAME_ANSWER: opens the window. invoke_game_on_question() has
// no IMU sample of its own, so the actual open happens here on the next tick.
static void enter_answer(const lsm6ds3_data_t *imu) {
    (void)imu; // no baseline to capture anymore — tilt_direction() is absolute
    s_aim_dir = 0;
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

void invoke_game_on_question(uint8_t answer_secs, const char *statement) {
    if (s_state != GAME_IDLE) {
        ESP_LOGI(TAG, "question ignored, one already in progress");
        return; // a question is already running; first one wins
    }

    s_answer_secs = answer_secs;
    if (s_answer_secs < MIN_ANSWER_S) s_answer_secs = MIN_ANSWER_S;
    if (s_answer_secs > MAX_ANSWER_S) s_answer_secs = MAX_ANSWER_S;
    snprintf(s_statement, sizeof(s_statement), "%s", statement ? statement : "");

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
                enter_answer(imu); // first tick: open the window
                return;
            }
            // Live aim preview: highlight the arrow the wrist is tilted
            // toward. This *is* the answer — there's no separate flick to
            // detect, so the full window always runs; whatever direction is
            // held (or none) when it closes is what gets sent below.
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
                if (s_aim_dir) invoke_mesh_send_gesture(s_aim_dir);
                enter_ack(s_aim_dir); // "" (0) shows NO ANSWER
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
