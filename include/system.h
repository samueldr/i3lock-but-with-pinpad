#ifndef _SYSTEM_H
#define _SYSTEM_H
#include <stdbool.h>

#ifndef SUSPEND_AFTER_SEC
#define SUSPEND_AFTER_SEC 60
#endif

void display_on();
void display_off();
void display_off_only();
bool is_display_on();

void input_mouse_on();
void input_mouse_off();

void system_teardown();
void system_signal_handler(const int signum);

#endif
