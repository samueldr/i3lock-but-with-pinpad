#ifndef _UNLOCK_INDICATOR_H
#define _UNLOCK_INDICATOR_H

#include <xcb/xcb.h>

typedef enum {
    STATE_STARTED = 0,           /* default state */
    STATE_KEY_PRESSED = 1,       /* key was pressed, show unlock indicator */
    STATE_KEY_ACTIVE = 2,        /* a key was pressed recently, highlight part
                                   of the unlock indicator. */
    STATE_BACKSPACE_ACTIVE = 3,  /* backspace was pressed recently, highlight
                                   part of the unlock indicator in red. */
    STATE_NOTHING_TO_DELETE = 4, /* backspace was pressed, but there is nothing to delete. */
    STATE_PAD_ACTIVE = 5, /* For the pin pad, so we don't leak numbers */
    STATE_PAD_BACKSPACE_ACTIVE = 6, /* For the pin pad, so it looks more correct. */
} unlock_state_t;

typedef enum {
    STATE_AUTH_IDLE = 0,          /* no authenticator interaction at the moment */
    STATE_AUTH_VERIFY = 1,        /* currently verifying the password via authenticator */
    STATE_AUTH_LOCK = 2,          /* currently locking the screen */
    STATE_AUTH_WRONG = 3,         /* the password was wrong */
    STATE_I3LOCK_LOCK_FAILED = 4, /* i3lock failed to load */
} auth_state_t;

typedef enum {
	PAD_BUTTON_INVALID = -1,
	PAD_BUTTON_0  =  0,
	PAD_BUTTON_1  =  1,
	PAD_BUTTON_2  =  2,
	PAD_BUTTON_3  =  3,
	PAD_BUTTON_4  =  4,
	PAD_BUTTON_5  =  5,
	PAD_BUTTON_6  =  6,
	PAD_BUTTON_7  =  7,
	PAD_BUTTON_8  =  8,
	PAD_BUTTON_9  =  9,
	PAD_BUTTON_BACKSPACE = 10,
	PAD_BUTTON_ZERO = 11,
	PAD_BUTTON_SEND = 12,
} pad_button_t;

void free_bg_pixmap(void);
void draw_image(xcb_pixmap_t bg_pixmap, uint32_t* resolution);
void redraw_screen(void);
void clear_indicator(void);
pad_button_t action_at(int32_t x, int32_t y);

#endif
