/*
 * Controls of the PC version: keyboard and Xbox controller (SDL reads XInput), with keys and
 * buttons remappable from the menu. Nothing here touches the simulated console: the result is only the PC-Engine
 * button byte (PCE_PAD_*) the window passes to the game, as it used to do with the keyboard.
 *
 * Each PC-Engine button has a key and a controller button. Default: arrows, X = I, Z = II, Space =
 * SELECT, Enter = RUN; on the controller, "Modern" layout: D-pad, A = I, B = II, Y = SELECT, X = RUN
 * (user's choice); "Original": B = I, A = II (position on the PC-Engine controller), the rest the same.
 * On screen the buttons are named after what they do in Ys ("Confirm/Magic", "Cancel/Item", "Commands", "Start").
 * The left stick is also the D-pad (50% dead zone, which leaves the diagonals at 45°). Esc and the
 * Menu button open the options menu and cannot be used for another function; the Xbox button belongs to Windows.
 * A key or button that already belonged to another function: the two swap (the other one takes the old one).
 *
 * In the config: key_up..key_run with the SDL key name ("Up", "X", "Return"...) and pad_up..pad_run
 * with the SDL button name ("dpup", "a", "b"...). An unknown or repeated name goes back to the default.
 */
#ifndef PCE_APP_INPUT_H
#define PCE_APP_INPUT_H

#include <stdint.h>

#include <SDL3/SDL.h>

#include "app/config.h"

typedef enum PadFunction {
    PAD_FN_UP,
    PAD_FN_DOWN,
    PAD_FN_LEFT,
    PAD_FN_RIGHT,
    PAD_FN_I,
    PAD_FN_II,
    PAD_FN_SELECT,
    PAD_FN_RUN,
    PAD_FN_COUNT
} PadFunction;

typedef struct InputMap {
    int key[PAD_FN_COUNT];    /* SDL_Scancode */
    int button[PAD_FN_COUNT]; /* SDL_GamepadButton */
} InputMap;

#define INPUT_MAX_PADS 4
#define INPUT_STICK_DEADZONE 16384 /* half the stick travel */

/* Connected controllers (opened on SDL_EVENT_GAMEPAD_ADDED) */
typedef struct InputPads {
    SDL_Gamepad *pad[INPUT_MAX_PADS];
    uint8_t stick[INPUT_MAX_PADS]; /* stick directions on the last event (for the menu) */
} InputPads;

/* Result of input_set_key/input_set_button */
#define INPUT_SET_OK 0
#define INPUT_SET_USED 1     /* already on another function (only when reading the .ini) */
#define INPUT_SET_RESERVED 2 /* Esc, Menu, Xbox button */
#define INPUT_SET_SWAPPED 3  /* was on another function: the two swapped keys/buttons */

const char *input_function_name(int fn); /* "Up", "Confirm/Magic", "Commands"... */
uint8_t input_function_bit(int fn);      /* PCE_PAD_* */

#define INPUT_LAYOUT_MODERN 0   /* default: A = I, B = II */
#define INPUT_LAYOUT_ORIGINAL 1 /* B = I, A = II */
#define INPUT_LAYOUT_CUSTOM 2   /* neither (some button remapped by hand) */
int input_layout(const InputMap *map);
void input_set_layout(InputMap *map, int layout); /* only the controller buttons */
const char *input_layout_name(int layout);

void input_default_keys(InputMap *map);
void input_default_buttons(InputMap *map);
/* Reads from the config; adds (and returns 1) when some key was missing or a value was invalid. */
int input_load(Config *cfg, InputMap *map);
void input_store(Config *cfg, const InputMap *map);

/* Changes the key/button of fn; if it already belonged to another function, that one takes fn's old one (other = which). */
int input_set_key(InputMap *map, int fn, int scancode, int *other);
int input_set_button(InputMap *map, int fn, int button, int *other);
/* Short names for the menu (e.g. "Enter", "Space", "B", "View") */
const char *input_key_name(int scancode);
const char *input_button_name(int button);

/* PC-Engine buttons pressed: keyboard (SDL_GetKeyboardState state) and controllers */
uint8_t input_keyboard_bits(const InputMap *map, const bool *keys);
uint8_t input_pads_bits(const InputPads *pads, const InputMap *map);

/* Connects/disconnects controllers (SDL_EVENT_GAMEPAD_ADDED/REMOVED); returns 1 if the event was one of those */
int input_pads_event(InputPads *pads, const SDL_Event *ev);
void input_pads_close(InputPads *pads);
/* Controller button pressed right now (for "the button that closes the menu does not reach the game") */
int input_pads_menu_pressed(const InputPads *pads);

/* Key/button/stick -> MenuKey (app/menu.h; -1 = none). Keyboard: Enter and the I key confirm,
 * Esc and the II key go back. Controller: A always confirms, B and Menu go back. */
int input_key_to_menu(const InputMap *map, int scancode);
int input_button_to_menu(const InputMap *map, int button);
/* Stick moved (SDL_EVENT_GAMEPAD_AXIS_MOTION): direction it just entered, or -1 */
int input_stick_to_menu(InputPads *pads, const SDL_Event *ev);

#endif
