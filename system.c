#include <stdio.h>
#include "system.h"
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
	system("light -I");
	system("xset dpms force on");
	display_state = true;
	input_mouse_on();
}

void display_off() {
	if (is_display_on()) {
		system("light -O");
	}
	system(
		"for f in /sys/class/backlight/*/brightness; do "
			"echo 0 > \"$f\";"
		"done"
	);
	display_off_only();
	input_mouse_off();
}

void display_off_only() {
	system("xset dpms force off");
	display_state = false;
}

bool is_display_on() {
	return display_state;
}

void input_mouse_on() {
	system(
		//"set -x;"
		"for i in "
			"$(xinput list | grep -v 'Virtual core' | grep 'floating\\s*slave' | sed --regexp-extended 's;.*id=([0-9]+)\\s*\\[.*;\\1;')"
		"; do "
		"xinput reattach $i 2;"
		"done"
	);
}

void input_mouse_off() {
	system(
		//"set -x;"
		"for i in "
			"$(xinput list | grep -v 'Virtual core' | grep '\\s\\+pointer' | sed --regexp-extended 's;.*id=([0-9]+)\\s*\\[.*;\\1;')"
		"; do "
		"xinput float $i;"
		"done"
	);
}

void system_teardown() {
	DEBUG("Tearing down...\n");
	display_on();
}

void system_signal_handler(const int signum) {
	system_teardown();
	exit(7);
}
