#include "widgets.h"
#include "tui_core.h"
#include "mm_fs.h"

static void memmove_impl(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
}

void tui_readline_init(tui_readline_t* rl, const char* prompt, uint8_t prompt_attr) {
    if (!rl) return;
    rl->prompt[0] = '\0';
    if (prompt) {
        size_t i = 0;
        while (prompt[i] && i < sizeof(rl->prompt) - 1) {
            rl->prompt[i] = prompt[i];
            i++;
        }
        rl->prompt[i] = '\0';
    }
    rl->prompt_attr = prompt_attr;
    rl->len = 0;
    rl->cursor = 0;
    rl->line[0] = '\0';
    rl->history_count = 0;
    rl->history_head = 0;
    rl->history_recall = 0;
    rl->saved_line[0] = '\0';
    rl->parent = NULL;
    for (int i = 0; i < TUI_READLINE_MAX_HISTORY; ++i) {
        rl->history[i][0] = '\0';
    }
}

void tui_readline_draw(tui_readline_t* rl) {
    if (!rl || !rl->parent) return;
    
    tui_surface_t* surf = &rl->parent->content;
    uint16_t row = rl->parent->h - 2;
    uint16_t col = 1;
    
    tui_surface_write(surf, row, 0, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4", tui_theme_attr(1));
    
    size_t prompt_len = 0;
    while (rl->prompt[prompt_len]) prompt_len++;
    
    tui_surface_write(surf, row, col, rl->prompt, rl->prompt_attr);
    col += (uint16_t)prompt_len;
    
    for (uint16_t i = 0; i < rl->len; ++i) {
        tui_surface_put(surf, row, col + i, rl->line[i], tui_theme_attr(4));
    }
    
    for (uint16_t i = rl->len; i < rl->parent->w - col - 2; ++i) {
        tui_surface_put(surf, row, col + i, ' ', tui_theme_attr(4));
    }
    
    if (rl->cursor < rl->len) {
        tui_surface_put(surf, row, col + rl->cursor, rl->line[rl->cursor], tui_make_attr(TUI_BLACK, TUI_WHITE));
    } else {
        tui_surface_put(surf, row, col + rl->cursor, '_', tui_make_attr(TUI_WHITE, TUI_BLUE));
    }
}

void tui_readline_key(tui_readline_t* rl, uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift) {
    (void)alt;
    (void)ctrl;
    (void)shift;
    
    if (!rl) return;
    
    if (scancode == 0x0E) {
        if (rl->cursor > 0) {
            memmove_impl(&rl->line[rl->cursor - 1], &rl->line[rl->cursor], rl->len - rl->cursor + 1);
            rl->cursor--;
            rl->len--;
        }
    } else if (scancode == 0x1C) {
    } else if (scancode == 0x47) {
        rl->cursor = 0;
    } else if (scancode == 0x4F) {
        rl->cursor = rl->len;
    } else if (scancode == 0x48) {
        if (rl->history_count > 0) {
            if (rl->history_recall == 0) {
                if (rl->len > 0) {
                    size_t i = 0;
                    while (i < rl->len && i < TUI_READLINE_MAX_LINE - 1) {
                        rl->saved_line[i] = rl->line[i];
                        i++;
                    }
                    rl->saved_line[i] = '\0';
                } else {
                    rl->saved_line[0] = '\0';
                }
            }
            if (rl->history_recall < rl->history_count) {
                uint8_t idx = (rl->history_head + TUI_READLINE_MAX_HISTORY - 1 - rl->history_recall) % TUI_READLINE_MAX_HISTORY;
                size_t i = 0;
                while (rl->history[idx][i] && i < TUI_READLINE_MAX_LINE - 1) {
                    rl->line[i] = rl->history[idx][i];
                    i++;
                }
                rl->line[i] = '\0';
                rl->len = (uint16_t)i;
                rl->cursor = rl->len;
                rl->history_recall++;
            }
        }
    } else if (scancode == 0x50) {
        if (rl->history_recall > 0) {
            rl->history_recall--;
            if (rl->history_recall == 0) {
                size_t i = 0;
                while (rl->saved_line[i] && i < TUI_READLINE_MAX_LINE - 1) {
                    rl->line[i] = rl->saved_line[i];
                    i++;
                }
                rl->line[i] = '\0';
                rl->len = (uint16_t)i;
                rl->cursor = rl->len;
            } else {
                uint8_t idx = (rl->history_head + TUI_READLINE_MAX_HISTORY - 1 - rl->history_recall) % TUI_READLINE_MAX_HISTORY;
                size_t i = 0;
                while (rl->history[idx][i] && i < TUI_READLINE_MAX_LINE - 1) {
                    rl->line[i] = rl->history[idx][i];
                    i++;
                }
                rl->line[i] = '\0';
                rl->len = (uint16_t)i;
                rl->cursor = rl->len;
            }
        }
    } else if (ascii >= 0x20 && ascii <= 0x7E) {
        if (rl->len < TUI_READLINE_MAX_LINE - 1) {
            memmove_impl(&rl->line[rl->cursor + 1], &rl->line[rl->cursor], rl->len - rl->cursor + 1);
            rl->line[rl->cursor] = (char)ascii;
            rl->cursor++;
            rl->len++;
        }
    } else if (scancode == 0x4B) {
        if (rl->cursor > 0) rl->cursor--;
    } else if (scancode == 0x4D) {
        if (rl->cursor < rl->len) rl->cursor++;
    }
}

const char* tui_readline_get_line(tui_readline_t* rl) {
    if (!rl) return "";
    return rl->line;
}

void tui_readline_clear(tui_readline_t* rl) {
    if (!rl) return;
    rl->len = 0;
    rl->cursor = 0;
    rl->line[0] = '\0';
    rl->history_recall = 0;
}

void tui_readline_add_history(tui_readline_t* rl, const char* line) {
    if (!rl || !line || line[0] == '\0') return;
    
    if (rl->history_count > 0) {
        uint8_t last_idx = (rl->history_head + TUI_READLINE_MAX_HISTORY - 1) % TUI_READLINE_MAX_HISTORY;
        size_t i = 0;
        int same = 1;
        while (rl->history[last_idx][i] && line[i]) {
            if (rl->history[last_idx][i] != line[i]) { same = 0; break; }
            i++;
        }
        if (same && rl->history[last_idx][i] == '\0' && line[i] == '\0') return;
    }
    
    size_t i = 0;
    while (line[i] && i < TUI_READLINE_MAX_LINE - 1) {
        rl->history[rl->history_head][i] = line[i];
        i++;
    }
    rl->history[rl->history_head][i] = '\0';
    
    rl->history_head = (rl->history_head + 1) % TUI_READLINE_MAX_HISTORY;
    if (rl->history_count < TUI_READLINE_MAX_HISTORY) rl->history_count++;
}