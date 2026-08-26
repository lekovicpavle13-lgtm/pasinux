#ifndef PS2MOUSE_H
#define PS2MOUSE_H

#include <stdint.h>

typedef struct {
    int16_t x, y;
    uint8_t buttons;
    int8_t dx, dy;
    uint8_t packet_ready;
} mouse_state_t;

void ps2mouse_init(void);
void ps2mouse_irq(void);
mouse_state_t* ps2mouse_get_state(void);
void ps2mouse_enable(void);
void ps2mouse_disable(void);

#endif