#include "tui_core.h"
#include "vga.h"
#include "mm_fs.h"
#include "io.h"
#include "serial.h"

static tui_surface_t g_screen;
/* Backing store for the composed frame: 80*25 cells, statically allocated so
 * tui_composite() never runs against an uninitialized surface. */
static tui_cell_t g_screen_cells[TUI_WIDTH * TUI_HEIGHT];
static uint16_t g_cursor_row = 0;
static uint16_t g_cursor_col = 0;
static uint8_t g_cursor_visible = 0;

static inline uint16_t vga_attr_from_tui(uint8_t attr) {
    return attr;
}

static inline uint8_t tui_attr_from_vga(uint16_t vga_attr) {
    return (uint8_t)vga_attr;
}

void tui_surface_init(tui_surface_t* surf, uint16_t width, uint16_t height) {
    surf->width = width;
    surf->height = height;
    size_t cells = (size_t)width * (size_t)height;
    surf->cells = (tui_cell_t*)kmalloc(cells * sizeof(tui_cell_t));
    if (surf->cells) {
        tui_surface_clear(surf, tui_make_attr(TUI_LIGHT_GRAY, TUI_BLACK));
    }
}

void tui_surface_clear(tui_surface_t* surf, uint8_t attr) {
    if (!surf || !surf->cells) return;
    size_t cells = (size_t)surf->width * (size_t)surf->height;
    for (size_t i = 0; i < cells; ++i) {
        surf->cells[i].ch = ' ';
        surf->cells[i].attr = attr;
    }
}

void tui_surface_write(tui_surface_t* surf, uint16_t row, uint16_t col, const char* text, uint8_t attr) {
    if (!surf || !surf->cells || !text) return;
    if (row >= surf->height) return;
    
    size_t pos = row * surf->width + col;
    for (size_t i = 0; text[i] != '\0' && pos < (size_t)surf->width * surf->height; ++i, ++pos) {
        if (pos >= (size_t)surf->width * surf->height) break;
        surf->cells[pos].ch = text[i];
        surf->cells[pos].attr = attr;
    }
}

void tui_surface_put(tui_surface_t* surf, uint16_t row, uint16_t col, char ch, uint8_t attr) {
    if (!surf || !surf->cells) return;
    if (row >= surf->height || col >= surf->width) return;
    
    size_t pos = row * surf->width + col;
    surf->cells[pos].ch = ch;
    surf->cells[pos].attr = attr;
}

void tui_surface_blit_to(tui_surface_t* src, tui_surface_t* dst,
                          uint16_t src_row, uint16_t src_col,
                          uint16_t dst_row, uint16_t dst_col,
                          uint16_t width, uint16_t height) {
    if (!src || !src->cells || !dst || !dst->cells) return;
    
    for (uint16_t r = 0; r < height; ++r) {
        uint16_t sr = src_row + r;
        uint16_t dr = dst_row + r;
        if (sr >= src->height || dr >= dst->height) continue;
        
        for (uint16_t c = 0; c < width; ++c) {
            uint16_t sc = src_col + c;
            uint16_t dc = dst_col + c;
            if (sc >= src->width || dc >= dst->width) continue;
            
            dst->cells[dr * dst->width + dc] = src->cells[sr * src->width + sc];
        }
    }
}

void tui_composite(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xC00B8000;
    
    cli();
    
    g_screen.cells = g_screen_cells;
    g_screen.width = TUI_WIDTH;
    g_screen.height = TUI_HEIGHT;
    
    tui_surface_clear(&g_screen, tui_theme_attr(0));
    
    extern void tui_wm_composite(tui_surface_t* screen);
    tui_wm_composite(&g_screen);
    
    if (g_cursor_visible) {
        if (g_cursor_row < TUI_HEIGHT && g_cursor_col < TUI_WIDTH) {
            size_t pos = g_cursor_row * TUI_WIDTH + g_cursor_col;
            uint16_t cell = g_screen.cells[pos].ch | (g_screen.cells[pos].attr << 8);
            cell = (cell & 0x00FF) | 0x7000;
            g_screen.cells[pos].ch = (uint8_t)cell;
            g_screen.cells[pos].attr = (uint8_t)(cell >> 8);
        }
    }
    
    for (size_t i = 0; i < (size_t)TUI_WIDTH * TUI_HEIGHT; ++i) {
        vga[i] = (uint16_t)(g_screen.cells[i].ch | (g_screen.cells[i].attr << 8));
    }
    
    sti();
}

void tui_cursor_show(uint16_t row, uint16_t col) {
    g_cursor_row = row;
    g_cursor_col = col;
    g_cursor_visible = 1;
}

void tui_cursor_hide(void) {
    g_cursor_visible = 0;
}

uint8_t tui_theme_attr(int element) {
    switch (element) {
        case 0: return tui_make_attr(TUI_LIGHT_GRAY, TUI_BLACK);        // desktop bg
        case 1: return tui_make_attr(TUI_CYAN, TUI_BLACK);              // window border
        case 2: return tui_make_attr(TUI_WHITE, TUI_BLUE);              // focused title bar
        case 3: return tui_make_attr(TUI_LIGHT_GRAY, TUI_BLUE);         // unfocused title bar
        case 4: return tui_make_attr(TUI_LIGHT_GRAY, TUI_BLACK);        // window content
        case 5: return tui_make_attr(TUI_WHITE, TUI_BLACK);             // shell text
        case 6: return tui_make_attr(TUI_LIGHT_RED, TUI_BLACK);         // errors
        case 7: return tui_make_attr(TUI_LIGHT_GREEN, TUI_BLACK);       // prompts
        default: return tui_make_attr(TUI_LIGHT_GRAY, TUI_BLACK);
    }
}

/* Headless self-test of the surface/compositing primitives the WM depends
 * on: bounded writes, clipped blits (source partially outside), and
 * bottom-to-top overlap ordering. Runs entirely off-screen and reports over
 * serial so it passes in -display none runs. */
void tui_selftest(void) {
    int fails = 0;
    const uint8_t attr_a = tui_make_attr(TUI_WHITE, TUI_BLUE);
    const uint8_t attr_b = tui_make_attr(TUI_YELLOW, TUI_RED);
    const uint8_t attr_c = tui_make_attr(TUI_BLACK, TUI_GREEN);

    tui_surface_t a, b, c;
    tui_surface_init(&a, 20, 10);
    tui_surface_init(&b, 40, 15);
    tui_surface_init(&c, 6, 3);
    if (!a.cells || !b.cells || !c.cells) {
        serial_puts("[TUI] compositor selftest: FAILED (alloc)\n");
        kfree(a.cells); kfree(b.cells); kfree(c.cells);
        return;
    }

    /* 1. clear + write + put */
    tui_surface_clear(&a, attr_a);
    tui_surface_write(&a, 1, 1, "HELLO", attr_b);
    if (a.cells[1 * 20 + 1].ch != 'H' || a.cells[1 * 20 + 5].ch != 'O' ||
        a.cells[1 * 20 + 1].attr != attr_b) ++fails;

    /* out-of-bounds writes must be dropped, not corrupt memory */
    tui_surface_put(&a, 10, 0, 'X', attr_c);
    tui_surface_put(&a, 0, 20, 'X', attr_c);
    tui_surface_write(&a, 99, 0, "XX", attr_c);

    /* 2. clipped blit: rows 8..12 of a (height 10) into b at row 8 —
     *    source rows 10..12 fall outside `a` and must be skipped */
    tui_surface_clear(&b, attr_b);
    tui_surface_blit_to(&a, &b, 8, 0, 8, 2, 20, 5);
    if (b.cells[8 * 40 + 2].ch != ' ') ++fails;          /* src row 8 = blank row */
    for (uint16_t r = 11; r < 13; ++r) {
        for (uint16_t col = 2; col < 22; ++col) {
            if (b.cells[r * 40 + col].attr != attr_b) { ++fails; r = 13; break; }
        }
    }

    /* 3. overlap ordering: write marker on a, blit a into b, then blit c
     *    over the same region — c must win where it covers */
    tui_surface_clear(&a, attr_a);
    tui_surface_write(&a, 2, 2, "MARKER", attr_b);
    tui_surface_blit_to(&a, &b, 0, 0, 0, 0, 20, 10);
    tui_surface_clear(&c, attr_c);
    tui_surface_blit_to(&c, &b, 0, 0, 2, 2, 4, 2);
    if (b.cells[2 * 40 + 2].ch != ' ' || b.cells[2 * 40 + 2].attr != attr_c) ++fails;
    if (b.cells[2 * 40 + 7].ch != 'R') ++fails;          /* outside c's clip */

    if (fails == 0) serial_puts("[TUI] compositor selftest: OK\n");
    else {
        serial_puts("[TUI] compositor selftest: FAILED (");
        char n[16]; int nd = 0; uint32_t v = (uint32_t)fails;
        if (v == 0u) n[nd++] = '0';
        while (v > 0u && nd < 15) { n[nd++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (nd > 0) serial_putc(n[--nd]);
        serial_puts(" checks)\n");
    }

    kfree(a.cells);
    kfree(b.cells);
    kfree(c.cells);
}