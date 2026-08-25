#ifndef TUI_CORE_H
#define TUI_CORE_H

#include <stdint.h>
#include <stddef.h>

#define TUI_WIDTH  80u
#define TUI_HEIGHT 25u

typedef enum {
    TUI_BLACK   = 0,
    TUI_BLUE    = 1,
    TUI_GREEN   = 2,
    TUI_CYAN    = 3,
    TUI_RED     = 4,
    TUI_MAGENTA = 5,
    TUI_BROWN   = 6,
    TUI_LIGHT_GRAY = 7,
    TUI_DARK_GRAY  = 8,
    TUI_LIGHT_BLUE = 9,
    TUI_LIGHT_GREEN = 10,
    TUI_LIGHT_CYAN = 11,
    TUI_LIGHT_RED  = 12,
    TUI_LIGHT_MAGENTA = 13,
    TUI_YELLOW = 14,
    TUI_WHITE  = 15
} tui_color_t;

typedef struct {
    uint8_t ch;
    uint8_t attr;
} tui_cell_t;

static inline uint8_t tui_make_attr(tui_color_t fg, tui_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

typedef struct {
    tui_cell_t* cells;
    uint16_t width;
    uint16_t height;
} tui_surface_t;

void tui_surface_init(tui_surface_t* surf, uint16_t width, uint16_t height);
void tui_surface_clear(tui_surface_t* surf, uint8_t attr);
void tui_surface_write(tui_surface_t* surf, uint16_t row, uint16_t col, const char* text, uint8_t attr);
void tui_surface_put(tui_surface_t* surf, uint16_t row, uint16_t col, char ch, uint8_t attr);
void tui_surface_blit_to(tui_surface_t* src, tui_surface_t* dst,
                          uint16_t src_row, uint16_t src_col,
                          uint16_t dst_row, uint16_t dst_col,
                          uint16_t width, uint16_t height);

void tui_composite(void);
void tui_cursor_show(uint16_t row, uint16_t col);
void tui_cursor_hide(void);

uint8_t tui_theme_attr(int element);

void tui_selftest(void);

#endif