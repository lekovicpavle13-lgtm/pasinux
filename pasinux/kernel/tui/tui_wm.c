#include "tui_wm.h"
#include "tui_core.h"
#include "mm_fs.h"
#include "keyboard.h"
#include "ps2mouse.h"
#include "serial.h"

static tui_win_t* g_windows[MAX_WINDOWS];
static uint8_t g_num_windows = 0;
static tui_win_t* g_focused = NULL;
static uint8_t g_next_z = 1;

#define KEY_QUEUE_SIZE 64
#define MOUSE_QUEUE_SIZE 32

static tui_event_t g_key_queue[KEY_QUEUE_SIZE];
static uint16_t g_key_head = 0, g_key_tail = 0;

static tui_event_t g_mouse_queue[MOUSE_QUEUE_SIZE];
static uint16_t g_mouse_head = 0, g_mouse_tail = 0;

static uint32_t g_queue_overflow = 0;

static int key_queue_full(void) {
    return ((g_key_head + 1) % KEY_QUEUE_SIZE) == g_key_tail;
}

static int key_queue_empty(void) {
    return g_key_head == g_key_tail;
}

static void key_queue_put(tui_event_t* ev) {
    if (key_queue_full()) {
        g_queue_overflow++;
        return;
    }
    g_key_queue[g_key_head] = *ev;
    g_key_head = (g_key_head + 1) % KEY_QUEUE_SIZE;
}

static tui_event_t* key_queue_get(void) {
    if (key_queue_empty()) return NULL;
    tui_event_t* ev = &g_key_queue[g_key_tail];
    g_key_tail = (g_key_tail + 1) % KEY_QUEUE_SIZE;
    return ev;
}

static int mouse_queue_full(void) {
    return ((g_mouse_head + 1) % MOUSE_QUEUE_SIZE) == g_mouse_tail;
}

static int mouse_queue_empty(void) {
    return g_mouse_head == g_mouse_tail;
}

static void mouse_queue_put(tui_event_t* ev) {
    if (mouse_queue_full()) {
        g_queue_overflow++;
        return;
    }
    g_mouse_queue[g_mouse_head] = *ev;
    g_mouse_head = (g_mouse_head + 1) % MOUSE_QUEUE_SIZE;
}

static tui_event_t* mouse_queue_get(void) {
    if (mouse_queue_empty()) return NULL;
    tui_event_t* ev = &g_mouse_queue[g_mouse_tail];
    g_mouse_tail = (g_mouse_tail + 1) % MOUSE_QUEUE_SIZE;
    return ev;
}

void tui_wm_post_key(uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift) {
    tui_event_t ev;
    ev.type = TUI_EV_KEY;
    ev.key.ascii = ascii;
    ev.key.scancode = scancode;
    ev.key.alt = alt;
    ev.key.ctrl = ctrl;
    ev.key.shift = shift;
    key_queue_put(&ev);
}

void tui_wm_post_mouse(uint8_t buttons, int8_t dx, int8_t dy) {
    tui_event_t ev;
    ev.type = TUI_EV_MOUSE;
    ev.mouse.buttons = buttons;
    ev.mouse.dx = dx;
    ev.mouse.dy = dy;
    mouse_queue_put(&ev);
}

uint32_t tui_wm_queue_overflow(void) {
    return g_queue_overflow;
}

void tui_wm_init(void) {
    g_num_windows = 0;
    g_focused = NULL;
    g_next_z = 1;
    g_key_head = g_key_tail = 0;
    g_mouse_head = g_mouse_tail = 0;
    g_queue_overflow = 0;
    for (int i = 0; i < MAX_WINDOWS; ++i) g_windows[i] = NULL;
}

tui_win_t* tui_win_create(const char* title, uint16_t w, uint16_t h) {
    if (g_num_windows >= MAX_WINDOWS) return NULL;
    
    tui_win_t* win = (tui_win_t*)kmalloc(sizeof(tui_win_t));
    if (!win) return NULL;
    
    win->x = (TUI_WIDTH - w) / 2;
    win->y = (TUI_HEIGHT - h) / 2;
    win->w = w;
    win->h = h;
    
    size_t title_len = 0;
    if (title) {
        while (title[title_len] && title_len < MAX_TITLE_LEN - 1) {
            win->title[title_len] = title[title_len];
            title_len++;
        }
    }
    win->title[title_len] = '\0';
    
    tui_surface_init(&win->content, w, h);
    if (!win->content.cells) {
        kfree(win);
        return NULL;
    }
    
    win->visible = 1;
    win->focused = 0;
    win->z_index = g_next_z++;
    win->pinned_bottom = 0;
    win->app_data = NULL;
    win->on_draw = NULL;
    win->on_key = NULL;
    win->on_mouse = NULL;
    
    g_windows[g_num_windows++] = win;
    serial_puts("[WM] create: ");
    serial_puts(win->title);
    serial_putc('\n');
    tui_win_raise(win);
    
    return win;
}

uint8_t tui_wm_window_count(void) {
    return g_num_windows;
}

void tui_win_destroy(tui_win_t* win) {
    if (!win) return;
    serial_puts("[WM] destroy: ");
    serial_puts(win->title);
    serial_putc('\n');
    
    for (int i = 0; i < g_num_windows; ++i) {
        if (g_windows[i] == win) {
            if (win->content.cells) kfree(win->content.cells);
            
            if (g_focused == win) {
                g_focused = NULL;
                for (int j = g_num_windows - 1; j >= 0; --j) {
                    if (g_windows[j] && g_windows[j] != win && g_windows[j]->visible) {
                        g_focused = g_windows[j];
                        g_focused->focused = 1;
                        break;
                    }
                }
            }
            
            for (int j = i; j < g_num_windows - 1; ++j) {
                g_windows[j] = g_windows[j + 1];
            }
            g_windows[g_num_windows - 1] = NULL;
            g_num_windows--;
            
            kfree(win);
            return;
        }
    }
}

void tui_win_raise(tui_win_t* win) {
    if (!win) return;
    /* Pinned windows (the fullscreen Shell) stay anchored at the bottom of
     * the z-order; they can take focus but never cover other windows. */
    if (!win->pinned_bottom) {
        win->z_index = g_next_z++;
    }
    if (g_focused != win) {
        if (g_focused) g_focused->focused = 0;
        win->focused = 1;
        g_focused = win;
    }
}

void tui_win_move(tui_win_t* win, int16_t dx, int16_t dy) {
    if (!win) return;
    
    int16_t new_x = (int16_t)win->x + dx;
    int16_t new_y = (int16_t)win->y + dy;
    
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if ((int)new_x + (int)win->w > (int)TUI_WIDTH) new_x = (int16_t)(TUI_WIDTH - win->w);
    if ((int)new_y + (int)win->h > (int)TUI_HEIGHT) new_y = (int16_t)(TUI_HEIGHT - win->h);
    
    win->x = (uint16_t)new_x;
    win->y = (uint16_t)new_y;
}

void tui_win_set_focus(tui_win_t* win) {
    if (!win) return;
    if (g_focused) g_focused->focused = 0;
    win->focused = 1;
    g_focused = win;
    tui_win_raise(win);
}

void tui_win_cycle_focus(void) {
    if (g_num_windows == 0) return;
    
    int start = -1;
    for (int i = 0; i < g_num_windows; ++i) {
        if (g_windows[i] == g_focused) {
            start = i;
            break;
        }
    }
    
    int next = (start + 1) % g_num_windows;
    while (next != start && (!g_windows[next] || !g_windows[next]->visible)) {
        next = (next + 1) % g_num_windows;
    }
    
    if (next != start && g_windows[next]) {
        tui_win_set_focus(g_windows[next]);
    }
}

static int win_contains(tui_win_t* w, uint16_t x, uint16_t y) {
    return w && w->visible &&
           x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + w->h;
}

static tui_win_t* win_at(uint16_t x, uint16_t y) {
    /* Topmost-first by z-order; pinned (bottom) windows only win when no
     * unpinned window covers the point. */
    tui_win_t* best = NULL;
    for (int i = 0; i < g_num_windows; ++i) {
        tui_win_t* w = g_windows[i];
        if (!w || w->pinned_bottom || !win_contains(w, x, y)) continue;
        if (!best || w->z_index > best->z_index) best = w;
    }
    if (best) return best;
    for (int i = 0; i < g_num_windows; ++i) {
        tui_win_t* w = g_windows[i];
        if (win_contains(w, x, y)) return w;
    }
    return NULL;
}

static void draw_window_border(tui_surface_t* screen, tui_win_t* win) {
    uint8_t border_attr = win->focused ? tui_theme_attr(2) : tui_theme_attr(3);
    const char ch_tl = (char)0xDA, ch_tr = (char)0xBF, ch_hz = (char)0xC4;
    const char ch_bl = (char)0xC0, ch_br = (char)0xD9, ch_vt = (char)0xB3;
    
    for (uint16_t c = 0; c < win->w; ++c) {
        tui_surface_put(screen, win->y, win->x + c,
                        c == 0 ? ch_tl : (c == win->w - 1 ? ch_tr : ch_hz), border_attr);
        tui_surface_put(screen, win->y + win->h - 1, win->x + c,
                        c == 0 ? ch_bl : (c == win->w - 1 ? ch_br : ch_hz), border_attr);
    }
    
    for (uint16_t r = 1; r < win->h - 1; ++r) {
        tui_surface_put(screen, win->y + r, win->x, ch_vt, border_attr);
        tui_surface_put(screen, win->y + r, win->x + win->w - 1, ch_vt, border_attr);
    }
    
    if (win->title[0]) {
        size_t title_len = 0;
        while (win->title[title_len]) title_len++;
        if (title_len > (size_t)(win->w - 2)) title_len = (size_t)(win->w - 2);
        
        for (size_t i = 0; i < title_len; ++i) {
            tui_surface_put(screen, win->y, win->x + 1 + i, win->title[i], border_attr);
        }
    }
    
    /* Let the app repaint its content surface before it is composited. */
    if (win->on_draw) {
        win->on_draw(&win->content, win);
    }

    for (uint16_t r = 1; r < win->h - 1; ++r) {
        for (uint16_t c = 1; c < win->w - 1; ++c) {
            tui_cell_t src = win->content.cells[r * win->w + c];
            tui_surface_put(screen, win->y + r, win->x + c, src.ch, src.attr);
        }
    }
}

void tui_wm_composite(tui_surface_t* screen) {
    tui_surface_clear(screen, tui_theme_attr(0));
    
    for (uint8_t z = 1; z <= g_next_z; ++z) {
        for (int i = 0; i < g_num_windows; ++i) {
            tui_win_t* w = g_windows[i];
            if (w && w->visible && w->z_index == z) {
                draw_window_border(screen, w);
            }
        }
    }
}

void tui_run(void) {
    serial_puts("[TUI] entering main event loop\n");
    
    for (;;) {
        tui_event_t* kev;
        while ((kev = key_queue_get()) != NULL) {
            /* WM-level chords first (design doc 4.3): Esc closes the focused
             * window, F2 opens About, F3 opens Clock, Alt+Tab cycles focus,
             * Alt+arrows move the focused window. */
            if (!kev->key.alt && !kev->key.ctrl && kev->key.scancode == 0x3C) {
                extern void tui_shell_open_about(void);
                serial_puts("[WM] F2: open About\n");
                tui_shell_open_about();
                continue;
            }
            if (!kev->key.alt && !kev->key.ctrl && kev->key.scancode == 0x3D) {
                extern void tui_shell_open_clock(void);
                serial_puts("[WM] F3: open Clock\n");
                tui_shell_open_clock();
                continue;
            }
            if (!kev->key.alt && kev->key.scancode == 0x01) {
                serial_puts("[WM] Esc: close focused\n");
                if (g_focused && g_num_windows > 1) {
                    tui_win_destroy(g_focused);
                }
                continue;
            }
            if (kev->key.alt && kev->key.scancode == 0x0F) {
                tui_win_cycle_focus();
            } else if (kev->key.alt) {
                if (kev->key.scancode == 0x48) {
                    if (g_focused) tui_win_move(g_focused, 0, -1);
                } else if (kev->key.scancode == 0x50) {
                    if (g_focused) tui_win_move(g_focused, 0, 1);
                } else if (kev->key.scancode == 0x4B) {
                    if (g_focused) tui_win_move(g_focused, -1, 0);
                } else if (kev->key.scancode == 0x4D) {
                    if (g_focused) tui_win_move(g_focused, 1, 0);
                }
            } else if (g_focused && g_focused->on_key) {
                g_focused->on_key(g_focused, kev->key.ascii, kev->key.scancode, 
                                 kev->key.alt, kev->key.ctrl, kev->key.shift);
            }
        }
        
        tui_event_t* mev;
        while ((mev = mouse_queue_get()) != NULL) {
            static int dragging = 0;
            static tui_win_t* drag_win = NULL;
            static int drag_offset_x = 0, drag_offset_y = 0;

            uint16_t cur_col = (uint16_t)ps2mouse_get_state()->x;
            uint16_t cur_row = (uint16_t)ps2mouse_get_state()->y;

            if (mev->mouse.buttons & 0x01) {
                tui_win_t* hit = win_at(cur_col, cur_row);
                serial_puts("[WM] mouse-down at ");
                { char n[8]; int nd=0; uint32_t v=cur_row; if(!v)n[nd++]='0'; while(v&&nd<7){n[nd++]=(char)('0'+v%10u);v/=10u;} while(nd>0)serial_putc(n[--nd]); }
                serial_putc(',');
                { char n[8]; int nd=0; uint32_t v=cur_col; if(!v)n[nd++]='0'; while(v&&nd<7){n[nd++]=(char)('0'+v%10u);v/=10u;} while(nd>0)serial_putc(n[--nd]); }
                serial_puts(" -> ");
                serial_puts(hit ? hit->title : "(none)");
                serial_putc('\n');
                if (hit) {
                    if (!dragging) {
                        tui_win_set_focus(hit);
                        if (!hit->pinned_bottom &&
                            cur_row == hit->y && cur_col >= hit->x && cur_col < hit->x + hit->w) {
                            dragging = 1;
                            drag_win = hit;
                            drag_offset_x = cur_col - hit->x;
                            drag_offset_y = cur_row - hit->y;
                            serial_puts("[WM] drag start\n");
                        }
                    } else if (dragging && drag_win) {
                        int16_t new_x = (int16_t)cur_col - drag_offset_x;
                        int16_t new_y = (int16_t)cur_row - drag_offset_y;
                        if (new_x < 0) new_x = 0;
                        if (new_y < 0) new_y = 0;
                        if ((int)new_x + (int)drag_win->w > (int)TUI_WIDTH)
                            new_x = (int16_t)(TUI_WIDTH - drag_win->w);
                        if ((int)new_y + (int)drag_win->h > (int)TUI_HEIGHT)
                            new_y = (int16_t)(TUI_HEIGHT - drag_win->h);
                        drag_win->x = (uint16_t)new_x;
                        drag_win->y = (uint16_t)new_y;
                    }
                }
            } else {
                dragging = 0;
                drag_win = NULL;
            }
        }
        
        tui_composite();
        
        __asm__ volatile ("hlt");
    }
}