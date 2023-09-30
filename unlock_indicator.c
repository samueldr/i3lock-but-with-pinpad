/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#if 0
#define WITH_DEBUG_RENDER
#endif

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)

#define WIDGET_RATIO_WIDTH 11
#define WIDGET_RATIO_HEIGHT 16
#define WIDGET_PADDING 16

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

extern char password[512];

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
extern int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;
/* Name of the current keyboard layout or NULL if not initialized. */
char *layout_string = NULL;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Whether keyboard layout should be displayed. */
extern bool show_keyboard_layout;
/* Number of failed unlock attempts. */
extern int failed_attempts;

extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

static void string_append(char **string_ptr, const char *appended) {
    char *tmp = NULL;
    if (*string_ptr == NULL) {
        if (asprintf(&tmp, "%s", appended) != -1) {
            *string_ptr = tmp;
        }
    } else if (asprintf(&tmp, "%s, %s", *string_ptr, appended) != -1) {
        free(*string_ptr);
        *string_ptr = tmp;
    }
}

static void display_button_text(
    cairo_t *ctx, const char *text, double y_offset, bool use_dark_text) {
    cairo_text_extents_t extents;
    double x, y;

    cairo_text_extents(ctx, text, &extents);
    x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
    y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + y_offset;

    cairo_move_to(ctx, x, y);
    if (use_dark_text) {
        cairo_set_source_rgb(ctx, 0., 0., 0.);
    } else {
        cairo_set_source_rgb(ctx, 1., 1., 1.);
    }
    cairo_show_text(ctx, text);
    cairo_close_path(ctx);
}

static void update_layout_string() {
    if (layout_string) {
        free(layout_string);
        layout_string = NULL;
    }
    xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(xkb_keymap);
    for (xkb_layout_index_t i = 0; i < num_layouts; ++i) {
        if (xkb_state_layout_index_is_active(xkb_state, i, XKB_STATE_LAYOUT_EFFECTIVE)) {
            const char *name = xkb_keymap_layout_get_name(xkb_keymap, i);
            if (name) {
                string_append(&layout_string, name);
            }
        }
    }
}

/* check_modifier_keys describes the currently active modifiers (Caps Lock, Alt,
   Num Lock or Super) in the modifier_string variable. */
static void check_modifier_keys(void) {
    xkb_mod_index_t idx, num_mods;
    const char *mod_name;

    num_mods = xkb_keymap_num_mods(xkb_keymap);

    for (idx = 0; idx < num_mods; idx++) {
        if (!xkb_state_mod_index_is_active(xkb_state, idx, XKB_STATE_MODS_EFFECTIVE))
            continue;

        mod_name = xkb_keymap_mod_get_name(xkb_keymap, idx);
        if (mod_name == NULL)
            continue;

        /* Replace certain xkb names with nicer, human-readable ones. */
        if (strcmp(mod_name, XKB_MOD_NAME_CAPS) == 0) {
            mod_name = "Caps Lock";
        } else if (strcmp(mod_name, XKB_MOD_NAME_NUM) == 0) {
            mod_name = "Num Lock";
        } else {
            /* Show only Caps Lock and Num Lock, other modifiers (e.g. Shift)
             * leak state about the password. */
            continue;
        }
        string_append(&modifier_string, mod_name);
    }
}

static void draw_classic_wheel(cairo_t *ctx) {
    const double scaling_factor = get_dpi_value() / 96.0;

    if (unlock_indicator &&
        (unlock_state >= STATE_KEY_PRESSED || auth_state > STATE_AUTH_IDLE)) {
        cairo_scale(ctx, scaling_factor, scaling_factor);
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, 10.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  0 /* start */,
                  2 * M_PI /* end */);

        /* Use the appropriate color for the different PAM states
         * (currently verifying, wrong password, or default) */
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgba(ctx, 0, 114.0 / 255, 255.0 / 255, 0.75);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);
                    break;
                }
                cairo_set_source_rgba(ctx, 0, 0, 0, 0.75);
                break;
        }
        cairo_fill_preserve(ctx);

        bool use_dark_text = true;

        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgb(ctx, 51.0 / 255, 0, 250.0 / 255);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgb(ctx, 125.0 / 255, 51.0 / 255, 0);
                break;
            case STATE_AUTH_IDLE:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgb(ctx, 125.0 / 255, 51.0 / 255, 0);
                    break;
                }

                cairo_set_source_rgb(ctx, 51.0 / 255, 125.0 / 255, 0);
                use_dark_text = false;
                break;
        }
        cairo_stroke(ctx);

        /* Draw an inner seperator line. */
        cairo_set_source_rgb(ctx, 0, 0, 0);
        cairo_set_line_width(ctx, 2.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS - 5 /* radius */,
                  0,
                  2 * M_PI);
        cairo_stroke(ctx);

        cairo_set_line_width(ctx, 10.0);

        /* Display a (centered) text of the current PAM state. */
        char *text = NULL;
        /* We don't want to show more than a 3-digit number. */
        char buf[4];

        cairo_set_source_rgb(ctx, 0, 0, 0);
        cairo_select_font_face(ctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(ctx, 28.0);
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
                text = "Verifying…";
                break;
            case STATE_AUTH_LOCK:
                text = "Locking…";
                break;
            case STATE_AUTH_WRONG:
                text = "Wrong!";
                break;
            case STATE_I3LOCK_LOCK_FAILED:
                text = "Lock failed!";
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    text = "No input";
                }
                if (show_failed_attempts && failed_attempts > 0) {
                    if (failed_attempts > 999) {
                        text = "> 999";
                    } else {
                        snprintf(buf, sizeof(buf), "%d", failed_attempts);
                        text = buf;
                    }
                    cairo_set_source_rgb(ctx, 1, 0, 0);
                    cairo_set_font_size(ctx, 32.0);
                }
                break;
        }

        if (text) {
            display_button_text(ctx, text, 0., use_dark_text);
        }

        if (modifier_string != NULL) {
            cairo_set_font_size(ctx, 14.0);
            display_button_text(ctx, modifier_string, 28., use_dark_text);
        }
        if (show_keyboard_layout && layout_string != NULL) {
            cairo_set_font_size(ctx, 14.0);
            display_button_text(ctx, layout_string, -28., use_dark_text);
        }

        /* After the user pressed any valid key or the backspace key, we
         * highlight a random part of the unlock indicator to confirm this
         * keypress. */
        if (unlock_state == STATE_KEY_ACTIVE ||
            unlock_state == STATE_BACKSPACE_ACTIVE) {
            cairo_new_sub_path(ctx);
#if 1
            double highlight_start = ((input_position) % (int)(2 * M_PI * 100)) / 1.0;
#else
            double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
#endif
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start,
                      highlight_start + (M_PI / 3.0));
            if (unlock_state == STATE_KEY_ACTIVE) {
                /* For normal keys, we use a lighter green. */
                cairo_set_source_rgb(ctx, 51.0 / 255, 219.0 / 255, 0);
            } else {
                /* For backspace, we use red. */
                cairo_set_source_rgb(ctx, 219.0 / 255, 51.0 / 255, 0);
            }
            cairo_stroke(ctx);

            /* Draw two little separators for the highlighted part of the
             * unlock indicator. */
            cairo_set_source_rgb(ctx, 0, 0, 0);
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start /* start */,
                      highlight_start + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      (highlight_start + (M_PI / 3.0)) - (M_PI / 128.0) /* start */,
                      highlight_start + (M_PI / 3.0) /* end */);
            cairo_stroke(ctx);
        }
    }
}

static void set_widget_dimensions(uint32_t* widget_width, uint32_t* widget_height) {
    uint32_t smallest_width = last_resolution[0];
    uint32_t smallest_height = last_resolution[1];

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            if (xr_resolutions[screen].width < smallest_width) {
                smallest_width = xr_resolutions[screen].width;
            }
            if (xr_resolutions[screen].height < smallest_height) {
                smallest_height = xr_resolutions[screen].height;
            }
        }
    }

    if (smallest_width < smallest_height) {
        // Portrait
        *widget_height = ceil(smallest_width * WIDGET_RATIO_HEIGHT / WIDGET_RATIO_WIDTH);
        *widget_width = smallest_width;
    }
    else {
        // Landscape
        *widget_width = ceil(smallest_height * WIDGET_RATIO_WIDTH / WIDGET_RATIO_HEIGHT);
        *widget_height = smallest_height;
    }

    *widget_height *= 0.9;
    *widget_width  *= 0.9;
}
static void set_widget_screen_position(Rect display, uint32_t *x, uint32_t *y) {
    uint32_t widget_width = 0;
    uint32_t widget_height = 0;

    set_widget_dimensions(&widget_width, &widget_height);

    // Target location of the widget surface on this display
    *x = (display.x) + ((display.width / 2)  - (widget_width / 2));
    *y = (display.y) + ((display.height / 2) - (widget_height / 2));
}

static void draw_pad_text(
    cairo_t *ctx
    , const char *text
    , double x
    , double y
    , double width
    , bool centered
) {
    cairo_text_extents_t extents;

    cairo_text_extents(ctx, text, &extents);
    y -= extents.y_bearing;
    if (centered) {
        x += width/2 - ((extents.width / 2) + extents.x_bearing);
    }

    cairo_move_to(ctx, x, y);
    cairo_show_text(ctx, text);
    cairo_close_path(ctx);
}

pad_button_t action_at(uint32_t x, uint32_t y) {
    pad_button_t ret = PAD_BUTTON_INVALID;
    int screen = -1;
    uint32_t widget_width = last_resolution[0];
    uint32_t widget_height = last_resolution[1];
    uint32_t widget_x = 0;
    uint32_t widget_y = 0;
    uint32_t action_x = 0;
    uint32_t action_y = 0;

    /*
     * Situate the event's screen
     */
    for (screen = 0; screen < xr_screens; screen++) {
        if (
               x > xr_resolutions[screen].x
            && x < xr_resolutions[screen].x + xr_resolutions[screen].width
            && y > xr_resolutions[screen].y
            && y < xr_resolutions[screen].y + xr_resolutions[screen].height
        ) {
            break;
        }
    }

    /*
     * On the screen, where the widget is
     */

    if (screen >= xr_screens) {
        DEBUG("!!!! Could not find screen!!\n");
        screen = 0;
    }

    /*
     * Find the whole container widget position and size
     */
    set_widget_dimensions(&widget_width, &widget_height);
    set_widget_screen_position(xr_resolutions[screen], &widget_x, &widget_y);

    // XXX This needs to be extracted into widget positioning/sizing logic
    // XXX see draw_pin_pad !!
    // Assumed to be the portrait layout for now...
    widget_y += widget_height - widget_width;
    widget_height = widget_width;

    widget_height -= 2 * WIDGET_PADDING;
    widget_width  -= 2 * WIDGET_PADDING;
    widget_x += WIDGET_PADDING;
    widget_y += WIDGET_PADDING;
    // ^^^^ XXX draw_pin_pad

    if (!(
           x > widget_x
        && x < widget_x + widget_width
        && y > widget_y
        && y < widget_y + widget_height
    )) {
        return ret;
    }

    action_x = x - widget_x;
    action_y = y - widget_y;

    ret = 1 + floor(3 * action_x / widget_width) + 3 * floor( 4 * action_y / widget_height);

    if (ret == PAD_BUTTON_ZERO) {
        ret = 0;
    }

    return ret;
}

void draw_button(
    cairo_t *ctx
    , uint32_t widget_width
    , uint32_t widget_height
    , uint32_t x
    , uint32_t y
    , uint32_t i
    , uint32_t j
    , double font_size
) {
    uint32_t num = i + 3*j;
    uint32_t button_width = floor(widget_width / 3);
    uint32_t button_height = floor(widget_height / 4);
    char text[16] = "";
    bool pressed = false;

    /*
     * Merge outlines
     */
    x += 1;
    y += 3;
    button_width -= 1;
    button_height -= 1;

    x += i*button_width;
    y += j*button_height;

    num += 1;
    switch (num) {
        case PAD_BUTTON_BACKSPACE:
            strncpy(text, "<=", 16);
            break;
        case PAD_BUTTON_ZERO:
            strncpy(text, "0", 16);
            break;
        case PAD_BUTTON_SEND:
            strncpy(text, ">>", 16);
            break;
        default:
            snprintf(text, 16, "%d", num);
            break;
    }

    if (unlock_state == STATE_PAD_ACTIVE) {
        if (password[input_position-1] == text[0]) {
            pressed = true;
        }
    }
    if (auth_state == STATE_AUTH_VERIFY && num == 12) {
        pressed = true;
    }
    if (unlock_state == STATE_PAD_BACKSPACE_ACTIVE && num == 10) {
        pressed = true;
    }

    cairo_rectangle(ctx, x, y, button_width, button_height);
    cairo_set_source_rgba(ctx, 0, 0, 0, 1);
    cairo_stroke(ctx);

    cairo_rectangle(ctx, x, y, button_width, button_height);
    cairo_set_source_rgba(ctx, 0, 0, 0, 0.1);
    if (pressed) {
        cairo_set_source_rgba(ctx, 0, 0, 0, 0.4);
    }
    cairo_fill(ctx);

    cairo_set_source_rgba(ctx, 0, 0, 0, 1);
    uint32_t middle = button_height / 2 - font_size/2 + 4;

    draw_pad_text(ctx, text, x, y + middle, button_width, true);
}

void draw_pin_pad(cairo_t *ctx) {
    cairo_surface_t *surface = (cairo_surface_t*)cairo_get_target(ctx);
    uint32_t widget_width = cairo_image_surface_get_width(surface);
    uint32_t widget_height = cairo_image_surface_get_height(surface);
    uint32_t x = 0;
    uint32_t y = 0;
    double font_size = 32;

    // Assumed to be the portrait layout for now...
    y = widget_height - widget_width;
    widget_height = widget_width;

    widget_height -= 2 * WIDGET_PADDING;
    widget_width  -= 2 * WIDGET_PADDING;
    x += WIDGET_PADDING;
    y += WIDGET_PADDING;

#ifdef WITH_DEBUG_RENDER
    cairo_set_source_rgb(ctx, 0, 1, 1);
    cairo_rectangle(ctx, x, y, widget_width, widget_height);
    cairo_fill(ctx);
#endif

    cairo_select_font_face(ctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(ctx, font_size);

    /*
     * The pad area is a matrix of 3×4 buttons.
     */
    cairo_set_line_width(ctx, 2.0);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            draw_button(ctx, widget_width, widget_height, x, y, i, j, font_size);
        }
    }
}

void draw_pin_box(cairo_t *ctx) {
    static char buf[512] = "";
    cairo_surface_t *surface = (cairo_surface_t*)cairo_get_target(ctx);
    uint32_t widget_width = cairo_image_surface_get_width(surface);
    uint32_t widget_height = cairo_image_surface_get_height(surface);
    uint32_t x = 0;
    uint32_t y = 0;
    double opa = 1;
    double font_size = 48;

    // Assumed to be the portrait layout for now...
    widget_height = widget_height - widget_width;

    widget_height -= 2 * WIDGET_PADDING;
    widget_width  -= 2 * WIDGET_PADDING;
    x += WIDGET_PADDING;
    y += WIDGET_PADDING;

#ifdef WITH_DEBUG_RENDER
    cairo_set_source_rgb(ctx, 1, 1, 0);
    cairo_rectangle(ctx, x, y, widget_width, widget_height);
    cairo_fill(ctx);
#endif

    buf[0] = 0;
    // TODO: replace with unicode string handling, and use 'BLACK CIRCLE' (U+25CF) ●●●●●●●
    for (int i = 0; i < input_position; i++) {
        buf[i] = '*';
        buf[i+1] = '\0';
    }

    switch (auth_state) {
        case STATE_AUTH_VERIFY:
            opa = 0.5;
            break;
        case STATE_AUTH_LOCK:
            strncpy(buf, "Locking…", 512);
            break;
        case STATE_AUTH_WRONG:
            strncpy(buf, "Wrong!", 512);
            break;
        case STATE_I3LOCK_LOCK_FAILED:
            strncpy(buf, "Lock failed!", 512);
            break;
        default:
    };

    // TODO Detect dark vs. light !!
    cairo_set_source_rgba(ctx, 0., 0., 0., opa);

    cairo_select_font_face(ctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(ctx, font_size);
    uint32_t middle = widget_height / 2 - font_size/2;
    draw_pad_text(ctx, buf, x, y + middle, widget_width, true);
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
void draw_image(xcb_pixmap_t bg_pixmap, uint32_t *resolution) {
    if (!vistype)
        vistype = get_root_visual_type(screen);

    uint32_t widget_width = last_resolution[0];
    uint32_t widget_height = last_resolution[1];

    set_widget_dimensions(&widget_width, &widget_height);

    /* Create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    /* After the first iteration, the pixmap will still contain the previous
     * contents. Explicitly clear the entire pixmap with the background color
     * first to get back into a defined state: */
    char strgroups[3][3] = {{color[0], color[1], '\0'},
                            {color[2], color[3], '\0'},
                            {color[4], color[5], '\0'}};
    uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                         (strtol(strgroups[1], NULL, 16)),
                         (strtol(strgroups[2], NULL, 16))};
    cairo_set_source_rgb(xcb_ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
    cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
    cairo_fill(xcb_ctx);

    /* Create one in-memory surface to render the unlock indicator on */
    cairo_surface_t *widget_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, widget_width, widget_height);
    cairo_t *ctx = cairo_create(widget_surface);

#ifdef WITH_DEBUG_RENDER
    cairo_set_source_rgb(ctx, 1, 0, 1);
    cairo_rectangle(ctx, 0, 0, widget_width, widget_height);
    cairo_fill(ctx);
#endif

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, 0, 0);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    }

    /*
     * Drawing the pin pad here
     */
    draw_pin_pad(ctx);

    /*
     * Drawing the pin box here
     */
    draw_pin_box(ctx);

#if 0
    draw_classic_wheel(ctx);
#endif

    /*
     * Rendering to displays
     */
    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x, y;
            set_widget_screen_position(xr_resolutions[screen], &x, &y);

            // The widget surface, copying from its origin
            cairo_set_source_surface(xcb_ctx, widget_surface, x, y);
            // To the target location
            cairo_rectangle(xcb_ctx, x, y, widget_width, widget_height);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (widget_width / 2);
        int y = (last_resolution[1] / 2) - (widget_height / 2);
        cairo_set_source_surface(xcb_ctx, widget_surface, x, y);
        cairo_rectangle(xcb_ctx, x, y, widget_width, widget_height);
        cairo_fill(xcb_ctx);
    }

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(widget_surface);
    cairo_destroy(ctx);
    cairo_destroy(xcb_ctx);
}

static xcb_pixmap_t bg_pixmap = XCB_NONE;

/*
 * Releases the current background pixmap so that the next redraw_screen() call
 * will allocate a new one with the updated resolution.
 *
 */
void free_bg_pixmap(void) {
    xcb_free_pixmap(conn, bg_pixmap);
    bg_pixmap = XCB_NONE;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);

    if (modifier_string) {
        free(modifier_string);
        modifier_string = NULL;
    }
    check_modifier_keys();
    update_layout_string();

    if (bg_pixmap == XCB_NONE) {
        DEBUG("allocating pixmap for %d x %d px\n", last_resolution[0], last_resolution[1]);
        bg_pixmap = create_bg_pixmap(conn, screen, last_resolution, color);
    }

    draw_image(bg_pixmap, last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}
