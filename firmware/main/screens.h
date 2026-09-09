#pragma once

#include "invoke_round.h"

#ifdef __cplusplus
extern "C" {
#endif

// The three INVOKE Band screens, drawn on the 128x128 ST7735. All text is
// expected to be printable ASCII already (run it through st7735_ascii_fold()
// upstream).

// WAIT: band name large, NUS service UUID small, "AGUARDANDO PERGUNTA".
void screens_wait(const char *uuid, const char *band_name);

// COUNTDOWN / ANSWER: wrapped statement, the 4 options as a list, a counter,
// and a time bar. `selected` is 'A'..'D' or 0 (none). Full redraw.
void screens_question(const round_q_t *q, char selected, int secs_left);

// Light updates during ANSWER (no full clear): move the highlight and shrink
// the time bar. Use after one screens_question() has drawn the frame.
void screens_question_select(char selected);
void screens_question_timebar(int secs_left, int secs_total);

// RESULT: "SUA RESPOSTA" + the letter big + the option text. letter 0 = none.
void screens_result(char letter, const char *text);

#ifdef __cplusplus
}
#endif
