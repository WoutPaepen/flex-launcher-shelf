#include <stdio.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include "launcher.h"
#include "util.h"
#include "clock.h"
#include "platform/platform.h"

extern Config config;

static TimeFormat time_format;
static DateFormat date_format;
static const char *WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// A function to get the time format for a region
static TimeFormat get_time_format(const char *region)
{
    const char *countries[] = { "US", "CA", "GB", "AU", "NZ", "IN" };
    for (size_t i = 0; i < sizeof(countries) / sizeof(countries[0]); i++) {
        if (!strcmp(region, countries[i]))
            return FORMAT_TIME_12HR;
    }
    return FORMAT_TIME_24HR;
}

// A function to get the date format for a region
static DateFormat get_date_format(const char *region)
{
    const char *countries[] = { "US", "JP", "CN" };
    for (size_t i = 0; i < sizeof(countries) / sizeof(countries[0]); i++) {
        if (!strcmp(region, countries[i]))
            return FORMAT_DATE_BIG;
    }
    return FORMAT_DATE_LITTLE;
}

void clock_init()
{
    time_format = config.clock_time_format;
    date_format = config.clock_date_format;
    if (time_format == FORMAT_TIME_AUTO || date_format == FORMAT_DATE_AUTO) {
        char region[3] = { 0 };
        get_region(region);
        if (time_format == FORMAT_TIME_AUTO)
            time_format = get_time_format(region);
        if (date_format == FORMAT_DATE_AUTO)
            date_format = get_date_format(region);
    }
}

bool clock_update(ClockText *out)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    ClockText c;
    memset(&c, 0, sizeof(c));
    if (time_format == FORMAT_TIME_12HR) {
        int h = t->tm_hour % 12;
        snprintf(c.time, sizeof(c.time), "%i:%02i", h == 0 ? 12 : h, t->tm_min);
        copy_string(c.period, t->tm_hour < 12 ? "AM" : "PM", sizeof(c.period));
    }
    else
        snprintf(c.time, sizeof(c.time), "%02i:%02i", t->tm_hour, t->tm_min);

    const char *wd = WEEKDAYS[t->tm_wday], *mon = MONTHS[t->tm_mon];
    if (date_format == FORMAT_DATE_BIG) {
        if (config.clock_include_weekday)
            snprintf(c.date, sizeof(c.date), "%s, %s %i", wd, mon, t->tm_mday);
        else
            snprintf(c.date, sizeof(c.date), "%s %i", mon, t->tm_mday);
    }
    else {
        if (config.clock_include_weekday)
            snprintf(c.date, sizeof(c.date), "%s %i %s", wd, t->tm_mday, mon);
        else
            snprintf(c.date, sizeof(c.date), "%i %s", t->tm_mday, mon);
    }
    bool changed = memcmp(&c, out, sizeof(c)) != 0;
    *out = c;
    return changed;
}
