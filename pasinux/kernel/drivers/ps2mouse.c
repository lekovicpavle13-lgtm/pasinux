#include "ps2mouse.h"
#include "io.h"
#include "interrupt.h"
#include "serial.h"
#include "tui_core.h"
#include "vga.h"

static mouse_state_t g_mouse_state = {0};
static uint8_t g_mouse_packet[3];
static uint8_t g_mouse_packet_byte = 0;
static uint32_t g_mouse_rx = 0;
static uint32_t g_mouse_bad = 0;

static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

static void ps2_wait_read(void) {
    for (int i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01) return;
    }
}

static void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(0x64, cmd);
}

static void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outb(0x60, data);
}

static uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(0x60);
}

static int ps2_mouse_send(uint8_t cmd) {
    ps2_write_cmd(0xD4);
    ps2_write_data(cmd);
    uint8_t resp = ps2_read_data();
    return resp == 0xFA;
}

void ps2mouse_init(void) {
    serial_puts("[MOUSE] initializing PS/2 mouse...\n");
    
    ps2_write_cmd(0xA8);
    
    ps2_write_cmd(0x20);
    uint8_t status = ps2_read_data();
    status |= 0x02;
    ps2_write_cmd(0x60);
    ps2_write_data(status);
    
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF6);
    uint8_t resp = ps2_read_data();
    
    if (resp != 0xFA) {
        serial_puts("[MOUSE] reset failed\n");
        return;
    }
    
    ps2_mouse_send(0xF4);
    
    ps2_mouse_send(0xE8);
    ps2_mouse_send(0x03);
    
    g_mouse_state.x = TUI_WIDTH / 2;
    g_mouse_state.y = TUI_HEIGHT / 2;
    g_mouse_state.buttons = 0;
    g_mouse_state.dx = 0;
    g_mouse_state.dy = 0;
    g_mouse_state.packet_ready = 0;
    g_mouse_packet_byte = 0;
    
    irq_register(12, ps2mouse_irq);
    serial_puts("[MOUSE] PS/2 mouse initialized on IRQ12\n");
}

void ps2mouse_irq(void) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0) {
        return;
    }
    /* Bit 5 of the status register: set when the byte in 0x60 came from the
     * mouse port. Without this check the mouse state machine can swallow
     * keyboard bytes (and vice versa), permanently desyncing the packet
     * stream. */
    if ((status & 0x20) == 0) {
        return;
    }
    
    uint8_t data = inb(0x60);
    
    if (g_mouse_packet_byte == 0) {
        if ((data & 0x08) == 0) {
            g_mouse_bad++;
            return;
        }
        if ((data & 0xC0) != 0) {
            g_mouse_bad++;
            return;
        }
        g_mouse_packet[0] = data;
        g_mouse_packet_byte = 1;
    } else if (g_mouse_packet_byte == 1) {
        g_mouse_packet[1] = data;
        g_mouse_packet_byte = 2;
    } else {
        g_mouse_packet[2] = data;
        g_mouse_packet_byte = 0;
        
        int8_t dx = (int8_t)g_mouse_packet[1];
        int8_t dy = (int8_t)g_mouse_packet[2];
        
        if (g_mouse_packet[0] & 0x10) dx |= 0xF0;
        if (g_mouse_packet[0] & 0x20) dy |= 0xF0;
        
        /* TEMP diagnostics: first 6 packets, log raw bytes */
        if (g_mouse_rx < 6u) {
            static const char digs[] = "0123456789ABCDEF";
            serial_puts("[MOUSEDBG] b0=");
            serial_putc(digs[g_mouse_packet[0] >> 4]);
            serial_putc(digs[g_mouse_packet[0] & 0xF]);
            serial_puts(" dx=");
            serial_putc(digs[(dx >> 4) & 0xF]);
            serial_putc(digs[dx & 0xF]);
            serial_puts(" dy=");
            serial_putc(digs[(dy >> 4) & 0xF]);
            serial_putc(digs[dy & 0xF]);
            serial_putc('\n');
        }
        
        g_mouse_state.dx = dx;
        g_mouse_state.dy = dy;
        g_mouse_state.buttons = g_mouse_packet[0] & 0x07;
        g_mouse_state.packet_ready = 1;
        
        g_mouse_rx++;
        
        int16_t new_x = g_mouse_state.x + dx / 4;
        int16_t new_y = g_mouse_state.y + dy / 4;
        
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        if (new_x >= (int16_t)TUI_WIDTH) new_x = (int16_t)TUI_WIDTH - 1;
        if (new_y >= (int16_t)TUI_HEIGHT) new_y = (int16_t)TUI_HEIGHT - 1;
        
        g_mouse_state.x = new_x;
        g_mouse_state.y = new_y;
        
        extern void tui_wm_post_mouse(uint8_t buttons, int8_t dx, int8_t dy);
        tui_wm_post_mouse(g_mouse_state.buttons, dx / 4, dy / 4);
    }
    
    pic_send_eoi(12);
}

mouse_state_t* ps2mouse_get_state(void) {
    return &g_mouse_state;
}

void ps2mouse_enable(void) {
    ps2_mouse_send(0xF4);
}

void ps2mouse_disable(void) {
    ps2_mouse_send(0xF5);
}