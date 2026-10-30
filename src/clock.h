// The clock in the top right corner: time, period and date as plain strings
#pragma once
#include <stdbool.h>

typedef struct {
    char time[16];    // "21:04" or "9:04"
    char period[8];   // "PM" in 12 hour format, else empty
    char date[40];    // "Fri 30 Oct" or "Fri, Oct 30"
} ClockText;

void clock_init(void);
// Fill in the current time; returns true when the text changed
bool clock_update(ClockText *out);
