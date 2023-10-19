#include <stdbool.h>
#include <stdio.h>
#include "i3lock.h"

extern bool debug_mode;
bool display_state = false;

/*
 * XXX
 * This file serves to insulate the hacks form the rest of the implementation...
 * We're shelling out to do those actions *for the moment* so that this is
 * ready to use quicker than it would be otherwise.
 */

void display_on() {
	system("xset dpms force on");
	display_state = true;
	input_mouse_off();
}

void display_off() {
	system("xset dpms force off");
	display_state = false;
	input_mouse_on();
}

bool is_display_on() {
	return display_state;
}

void input_mouse_on() {
}

void input_mouse_off() {
}
