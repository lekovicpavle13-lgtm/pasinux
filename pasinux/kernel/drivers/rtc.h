#ifndef RTC_H
#define RTC_H

#include <stdint.h>

int rtc_init(void);
int rtc_read_time(int* hours, int* minutes, int* seconds);

#endif