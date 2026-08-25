#ifndef TUI_WM_H
#define TUI_WM_H

#include <stdint.h>
#include "tui_core.h"

#define MAX_WINDOWS 16
#define MAX_TITLE_LEN 32

typedef struct tui_win tui_win_t;

typedef void (*tui_draw_cb_t)(tui_surface_t* surf, tui_win_t* win);
typedef void (*tui_key_cb_t)(tui_win_t* win, uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift);
typedef void (*tui_mouse_cb_t)(tui_win_t* win, uint8_t buttons, int8_t dx, int8_t dy);

struct tui_win {
    uint16_t x, y;
    uint16_t w, h;
    char title[MAX_TITLE_LEN];
    tui_surface_t content;
    uint8_t visible;
    uint8_t focused;
    uint8_t z_index;
    uint8_t pinned_bottom;
    void* app_data;
    tui_draw_cb_t on_draw;
    tui_key_cb_t on_key;
    tui_mouse_cb_t on_mouse;
};

typedef enum {
    TUI_EV_KEY,
    TUI_EV_MOUSE
} tui_event_type_t;

typedef struct {
    tui_event_type_t type;
    union {
        struct { uint8_t ascii, scancode, alt, ctrl, shift; } key;
        struct { uint8_t buttons; int8_t dx, dy; } mouse;
    };
} tui_event_t;

void tui_wm_init(void);
tui_win_t* tui_win_create(const char* title, uint16_t w, uint16_t h);
void tui_win_destroy(tui_win_t* win);
void tui_win_raise(tui_win_t* win);
void tui_win_move(tui_win_t* win, int16_t dx, int16_t dy);
void tui_win_set_focus(tui_win_t* win);
void tui_win_cycle_focus(void);
void tui_run(void);
void tui_wm_composite(tui_surface_t* screen);

void tui_wm_post_key(uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift);
void tui_wm_post_mouse(uint8_t buttons, int8_t dx, int8_t dy);

uint32_t tui_wm_queue_overflow(void);
uint8_t tui_wm_window_count(void);

#endif