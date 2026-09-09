#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "st7735.h"

#define AMBER ST7735_RGB565(255, 179, 71)
#define DGRAY ST7735_RGB565(40, 40, 40)

#define SCREEN_W ST7735_WIDTH
#define SCREEN_H ST7735_HEIGHT

// Option-list geometry (QUESTION / ANSWER screen).
static const int16_t OPT_Y[4] = {42, 63, 84, 105};
#define OPT_H 20
#define BAR_Y 126
#define BAR_H 2
#define CNT_X 100
#define CNT_W 28

static const round_q_t *s_q;   // set by screens_question(), used by updaters
static int s_total_secs = 1;

static void draw_centered(int16_t y, const char *s, uint16_t fg, uint16_t bg,
                          uint8_t scale) {
  int16_t w = st7735_text_width(s, scale);
  int16_t x = (int16_t)((SCREEN_W - w) / 2);
  if (x < 0) {
    x = 0;
  }
  st7735_draw_text(x, y, s, fg, bg, scale);
}

// ---------------------------------------------------------------- WAIT --------

void screens_wait(const char *uuid, const char *band_name) {
  st7735_fill_screen(ST7735_BLACK);
  draw_centered(8, "PRONTA", ST7735_GREEN, ST7735_BLACK, 1);
  draw_centered(26, band_name, ST7735_WHITE, ST7735_BLACK, 2);
  draw_centered(56, "SERVICO NUS", ST7735_GRAY, ST7735_BLACK, 1);
  st7735_draw_text_wrapped(2, 70, SCREEN_W - 4, 9, uuid, AMBER, ST7735_BLACK, 1);
  draw_centered(104, "AGUARDANDO", ST7735_GRAY, ST7735_BLACK, 1);
  draw_centered(114, "PERGUNTA", ST7735_GRAY, ST7735_BLACK, 1);
}

// ------------------------------------------------------------ QUESTION --------

static void draw_counter(int secs_left) {
  char n[12];
  int v = secs_left < 0 ? 0 : secs_left > 999 ? 999 : secs_left;
  snprintf(n, sizeof(n), "%d", v);
  st7735_fill_rect(CNT_X, 0, CNT_W, 16, ST7735_BLACK);
  int16_t w = st7735_text_width(n, 2);
  st7735_draw_text((int16_t)(SCREEN_W - 2 - w), 1, n, AMBER, ST7735_BLACK, 2);
}

static void draw_option_row(int i, char selected) {
  if (!s_q) {
    return;
  }
  char letter = (char)('A' + i);
  bool on = (selected == letter);
  uint16_t bg = on ? AMBER : ST7735_BLACK;
  uint16_t fg = on ? ST7735_BLACK : ST7735_WHITE;
  uint16_t lfg = on ? ST7735_BLACK : AMBER;

  st7735_fill_rect(0, OPT_Y[i], SCREEN_W, OPT_H, bg);

  char lbuf[2] = {letter, '\0'};
  st7735_draw_text(4, (int16_t)(OPT_Y[i] + 3), lbuf, lfg, bg, 2);

  char txt[20];
  strncpy(txt, s_q->opt[i], sizeof(txt) - 1);
  txt[sizeof(txt) - 1] = '\0';
  st7735_draw_text(22, (int16_t)(OPT_Y[i] + 7), txt, fg, bg, 1);
}

void screens_question(const round_q_t *q, char selected, int secs_left) {
  s_q = q;
  s_total_secs = q->to > 0 ? q->to : 1;

  st7735_fill_screen(ST7735_BLACK);
  st7735_draw_text_wrapped(2, 2, SCREEN_W - 4, 9, q->stmt, ST7735_WHITE,
                           ST7735_BLACK, 1);
  // keep the option area clean even if the statement wrapped long
  st7735_fill_rect(0, OPT_Y[0] - 2, SCREEN_W, SCREEN_H - (OPT_Y[0] - 2),
                   ST7735_BLACK);
  for (int i = 0; i < 4; i++) {
    draw_option_row(i, selected);
  }
  screens_question_timebar(secs_left, s_total_secs);
  draw_counter(secs_left);
}

void screens_question_select(char selected) {
  for (int i = 0; i < 4; i++) {
    draw_option_row(i, selected);
  }
}

void screens_question_timebar(int secs_left, int secs_total) {
  if (secs_total <= 0) {
    secs_total = 1;
  }
  if (secs_left < 0) {
    secs_left = 0;
  }
  if (secs_left > secs_total) {
    secs_left = secs_total;
  }
  int16_t fill = (int16_t)((int32_t)SCREEN_W * secs_left / secs_total);
  st7735_fill_rect(0, BAR_Y, fill, BAR_H, AMBER);
  st7735_fill_rect(fill, BAR_Y, (int16_t)(SCREEN_W - fill), BAR_H, DGRAY);
  draw_counter(secs_left);
}

// ------------------------------------------------------------- RESULT --------

void screens_result(char letter, const char *text) {
  st7735_fill_screen(ST7735_BLACK);
  draw_centered(10, "SUA RESPOSTA", ST7735_GRAY, ST7735_BLACK, 1);

  if (letter >= 'A' && letter <= 'D') {
    char l[2] = {letter, '\0'};
    draw_centered(26, l, ST7735_GREEN, ST7735_BLACK, 6);
    if (text && text[0]) {
      st7735_draw_text_wrapped(2, 80, SCREEN_W - 4, 10, text, ST7735_WHITE,
                               ST7735_BLACK, 1);
    }
  } else {
    draw_centered(48, "SEM RESPOSTA", ST7735_RED, ST7735_BLACK, 2);
  }
}
