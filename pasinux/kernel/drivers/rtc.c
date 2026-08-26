#include "rtc.h"
#include "io.h"
#include "serial.h"

static inline uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg | 0x80);
    io_wait();
    return inb(0x71);
}

int rtc_init(void) {
    serial_puts("[RTC] initializing CMOS RTC...\n");
    
    uint8_t reg_b = cmos_read(0x0B);
    
    if (!(reg_b & 0x04)) {
        serial_puts("[RTC] 12-hour mode, converting to 24-hour\n");
    }
    if (!(reg_b & 0x02)) {
        serial_puts("[RTC] BCD mode\n");
    }
    
    serial_puts("[RTC] CMOS RTC initialized\n");
    return 0;
}

int rtc_read_time(int* hours, int* minutes, int* seconds) {
    uint8_t sec, min, hr;
    uint8_t reg_a;
    
    do {
        reg_a = cmos_read(0x0A);
    } while (reg_a & 0x80);
    
    sec = cmos_read(0x00);
    min = cmos_read(0x02);
    hr = cmos_read(0x04);
    
    uint8_t reg_b = cmos_read(0x0B);
    
    if (!(reg_b & 0x02)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr = bcd_to_bin(hr);
    }
    
    if (!(reg_b & 0x04)) {
        if (hr & 0x80) {
            hr = (hr & 0x7F) + 12;
        }
    }
    
    *hours = hr;
    *minutes = min;
    *seconds = sec;
    
    return 0;
}