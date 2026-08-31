#ifndef TUI_WIDGETS_H
#define TUI_WIDGETS_H

#include <stdint.h>
#include "tui_core.h"
#include "tui_wm.h"

#define TUI_READLINE_MAX_HISTORY 32
#define TUI_READLINE_MAX_LINE 256

typedef struct {
    char prompt[64];
    uint8_t prompt_attr;
    char line[TUI_READLINE_MAX_LINE];
    uint16_t len;
    uint16_t cursor;
    char history[TUI_READLINE_MAX_HISTORY][TUI_READLINE_MAX_LINE];
    char saved_line[TUI_READLINE_MAX_LINE];
    uint8_t history_count;
    uint8_t history_head;
    uint8_t history_recall;
    tui_win_t* parent;
} tui_readline_t;

void tui_readline_init(tui_readline_t* rl, const char* prompt, uint8_t prompt_attr);
void tui_readline_draw(tui_readline_t* rl);
void tui_readline_key(tui_readline_t* rl, uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift);
const char* tui_readline_get_line(tui_readline_t* rl);
void tui_readline_clear(tui_readline_t* rl);
void tui_readline_add_history(tui_readline_t* rl, const char* line);

#endif