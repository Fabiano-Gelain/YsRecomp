#include "app/input.h"

#include <stdio.h>
#include <string.h>

#include "app/menu.h"
#include "pce/pce.h"

/* Names by what the button does in Ys (manual): I confirms and uses magic (Ys II), II uses the item and cancels,
 * SELECT pauses and opens the command window, RUN starts the game */
static const char *const k_names[PAD_FN_COUNT] = { "Up", "Down", "Left", "Right",
                                                   "Confirm/Magic", "Cancel/Item", "Commands", "Start" };
static const char *const k_config_keys[PAD_FN_COUNT] = { "key_up", "key_down", "key_left", "key_right",
                                                         "key_i", "key_ii", "key_select", "key_run" };
static const char *const k_config_pads[PAD_FN_COUNT] = { "pad_up", "pad_down", "pad_left", "pad_right",
                                                         "pad_i", "pad_ii", "pad_select", "pad_run" };
static const uint8_t k_bits[PAD_FN_COUNT] = { PCE_PAD_UP, PCE_PAD_DOWN, PCE_PAD_LEFT, PCE_PAD_RIGHT,
                                              PCE_PAD_I, PCE_PAD_II, PCE_PAD_SELECT, PCE_PAD_RUN };
static const int k_default_keys[PAD_FN_COUNT] = { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
                                                  SDL_SCANCODE_RIGHT, SDL_SCANCODE_X, SDL_SCANCODE_Z,
                                                  SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN };
static const int k_default_buttons[PAD_FN_COUNT] = {
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_SOUTH /* A */, SDL_GAMEPAD_BUTTON_EAST /* B */,
    SDL_GAMEPAD_BUTTON_NORTH /* Y */, SDL_GAMEPAD_BUTTON_WEST /* X */
};

/* "Original" layout: I and II in the position of the PC-Engine controller (II on the left, I on the right) */
static const int k_original_buttons[PAD_FN_COUNT] = {
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_EAST /* B */, SDL_GAMEPAD_BUTTON_SOUTH /* A */,
    SDL_GAMEPAD_BUTTON_NORTH /* Y */, SDL_GAMEPAD_BUTTON_WEST /* X */
};

int input_layout(const InputMap *map)
{
    if (memcmp(map->button, k_default_buttons, sizeof(map->button)) == 0)
        return INPUT_LAYOUT_MODERN;
    if (memcmp(map->button, k_original_buttons, sizeof(map->button)) == 0)
        return INPUT_LAYOUT_ORIGINAL;
    return INPUT_LAYOUT_CUSTOM;
}

void input_set_layout(InputMap *map, int layout)
{
    memcpy(map->button, layout == INPUT_LAYOUT_ORIGINAL ? k_original_buttons : k_default_buttons, sizeof(map->button));
}

const char *input_layout_name(int layout)
{
    return layout == INPUT_LAYOUT_MODERN ? "Modern" : layout == INPUT_LAYOUT_ORIGINAL ? "Original" : "Custom";
}

const char *input_function_name(int fn)
{
    return fn >= 0 && fn < PAD_FN_COUNT ? k_names[fn] : "";
}

uint8_t input_function_bit(int fn)
{
    return fn >= 0 && fn < PAD_FN_COUNT ? k_bits[fn] : 0;
}

void input_default_keys(InputMap *map)
{
    memcpy(map->key, k_default_keys, sizeof(map->key));
}

void input_default_buttons(InputMap *map)
{
    memcpy(map->button, k_default_buttons, sizeof(map->button));
}

static int key_reserved(int sc)
{
    return sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT || sc == SDL_SCANCODE_ESCAPE;
}

static int button_reserved(int b)
{
    return b <= SDL_GAMEPAD_BUTTON_INVALID || b >= SDL_GAMEPAD_BUTTON_COUNT || b == SDL_GAMEPAD_BUTTON_START ||
           b == SDL_GAMEPAD_BUTTON_GUIDE;
}

/* Puts value in slots[fn]; if another function already uses it, the two swap (swap) or it is refused (reading the .ini) */
static int assign(int *slots, int fn, int value, int swap, int *other)
{
    for (int i = 0; i < PAD_FN_COUNT; i++)
        if (i != fn && slots[i] == value) {
            if (!swap)
                return INPUT_SET_USED;
            slots[i] = slots[fn];
            slots[fn] = value;
            if (other)
                *other = i;
            return INPUT_SET_SWAPPED;
        }
    slots[fn] = value;
    return INPUT_SET_OK;
}

int input_set_key(InputMap *map, int fn, int scancode, int *other)
{
    return key_reserved(scancode) ? INPUT_SET_RESERVED : assign(map->key, fn, scancode, 1, other);
}

int input_set_button(InputMap *map, int fn, int button, int *other)
{
    return button_reserved(button) ? INPUT_SET_RESERVED : assign(map->button, fn, button, 1, other);
}

int input_load(Config *cfg, InputMap *map)
{
    InputMap got;
    input_default_keys(&got);
    input_default_buttons(&got);
    int bad = 0;
    for (int i = 0; i < PAD_FN_COUNT; i++) {
        const char *k = config_get(cfg, k_config_keys[i], NULL);
        const char *b = config_get(cfg, k_config_pads[i], NULL);
        got.key[i] = k ? (int)SDL_GetScancodeFromName(k) : -1;
        got.button[i] = b ? (int)SDL_GetGamepadButtonFromString(b) : -1;
    }
    /* Invalid or repeated: the function goes back to the default (if the default is also taken, everything goes back) */
    InputMap out;
    input_default_keys(&out);
    input_default_buttons(&out);
    for (int i = 0; i < PAD_FN_COUNT; i++)
        out.key[i] = out.button[i] = -1;
    for (int i = 0; i < PAD_FN_COUNT; i++) {
        if (key_reserved(got.key[i]) || assign(out.key, i, got.key[i], 0, NULL) != INPUT_SET_OK) {
            bad = 1;
            out.key[i] = -1;
        }
        if (button_reserved(got.button[i]) || assign(out.button, i, got.button[i], 0, NULL) != INPUT_SET_OK) {
            bad = 1;
            out.button[i] = -1;
        }
    }
    for (int i = 0; i < PAD_FN_COUNT; i++) {
        if (out.key[i] < 0 && assign(out.key, i, k_default_keys[i], 0, NULL) != INPUT_SET_OK)
            input_default_keys(&out);
        if (out.button[i] < 0 && assign(out.button, i, k_default_buttons[i], 0, NULL) != INPUT_SET_OK)
            input_default_buttons(&out);
    }
    *map = out;
    if (bad)
        input_store(cfg, map);
    return bad;
}

void input_store(Config *cfg, const InputMap *map)
{
    for (int i = 0; i < PAD_FN_COUNT; i++) {
        config_set(cfg, k_config_keys[i], SDL_GetScancodeName((SDL_Scancode)map->key[i]));
        const char *b = SDL_GetGamepadStringForButton((SDL_GamepadButton)map->button[i]);
        config_set(cfg, k_config_pads[i], b ? b : "");
    }
}

const char *input_key_name(int scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_KP_ENTER: return "Pad Enter";
    case SDL_SCANCODE_LSHIFT: return "L Shift";
    case SDL_SCANCODE_RSHIFT: return "R Shift";
    case SDL_SCANCODE_LCTRL: return "L Ctrl";
    case SDL_SCANCODE_RCTRL: return "R Ctrl";
    case SDL_SCANCODE_LALT: return "L Alt";
    case SDL_SCANCODE_RALT: return "R Alt";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    default: break;
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);
    return name && *name ? name : "?";
}

const char *input_button_name(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "View";
    case SDL_GAMEPAD_BUTTON_START: return "Menu";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L stick";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R stick";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Pad Right";
    case SDL_GAMEPAD_BUTTON_MISC1: return "Share";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "Paddle 1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "Paddle 2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "Paddle 3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "Paddle 4";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    default: return "?";
    }
}

uint8_t input_keyboard_bits(const InputMap *map, const bool *keys)
{
    uint8_t b = 0;
    for (int i = 0; i < PAD_FN_COUNT; i++)
        if (map->key[i] > 0 && map->key[i] < SDL_SCANCODE_COUNT && keys[map->key[i]])
            b |= k_bits[i];
    return b;
}

static uint8_t stick_bits(SDL_Gamepad *pad)
{
    int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    uint8_t b = 0;
    if (x <= -INPUT_STICK_DEADZONE) b |= PCE_PAD_LEFT;
    if (x >= INPUT_STICK_DEADZONE) b |= PCE_PAD_RIGHT;
    if (y <= -INPUT_STICK_DEADZONE) b |= PCE_PAD_UP;
    if (y >= INPUT_STICK_DEADZONE) b |= PCE_PAD_DOWN;
    return b;
}

uint8_t input_pads_bits(const InputPads *pads, const InputMap *map)
{
    uint8_t b = 0;
    for (int p = 0; p < INPUT_MAX_PADS; p++) {
        SDL_Gamepad *pad = pads->pad[p];
        if (!pad)
            continue;
        for (int i = 0; i < PAD_FN_COUNT; i++)
            if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)map->button[i]))
                b |= k_bits[i];
        b |= stick_bits(pad);
    }
    return b;
}

int input_pads_menu_pressed(const InputPads *pads)
{
    for (int p = 0; p < INPUT_MAX_PADS; p++)
        if (pads->pad[p] && SDL_GetGamepadButton(pads->pad[p], SDL_GAMEPAD_BUTTON_START))
            return 1;
    return 0;
}

int input_pads_event(InputPads *pads, const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (int p = 0; p < INPUT_MAX_PADS; p++)
            if (pads->pad[p] && SDL_GetGamepadID(pads->pad[p]) == ev->gdevice.which)
                return 1;
        for (int p = 0; p < INPUT_MAX_PADS; p++)
            if (!pads->pad[p]) {
                pads->pad[p] = SDL_OpenGamepad(ev->gdevice.which);
                pads->stick[p] = 0;
                if (pads->pad[p])
                    printf("Controller connected: %s\n", SDL_GetGamepadName(pads->pad[p]));
                break;
            }
        return 1;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int p = 0; p < INPUT_MAX_PADS; p++)
            if (pads->pad[p] && SDL_GetGamepadID(pads->pad[p]) == ev->gdevice.which) {
                SDL_CloseGamepad(pads->pad[p]);
                pads->pad[p] = NULL;
                printf("Controller disconnected.\n");
            }
        return 1;
    }
    return 0;
}

void input_pads_close(InputPads *pads)
{
    for (int p = 0; p < INPUT_MAX_PADS; p++)
        if (pads->pad[p]) {
            SDL_CloseGamepad(pads->pad[p]);
            pads->pad[p] = NULL;
        }
}

int input_key_to_menu(const InputMap *map, int scancode)
{
    static const int dirs[4] = { MENU_KEY_UP, MENU_KEY_DOWN, MENU_KEY_LEFT, MENU_KEY_RIGHT };
    for (int i = PAD_FN_UP; i <= PAD_FN_RIGHT; i++)
        if (map->key[i] == scancode)
            return dirs[i];
    if (map->key[PAD_FN_I] == scancode || map->key[PAD_FN_RUN] == scancode)
        return MENU_KEY_OK;
    if (map->key[PAD_FN_II] == scancode)
        return MENU_KEY_BACK;
    switch (scancode) {
    case SDL_SCANCODE_UP: return MENU_KEY_UP;
    case SDL_SCANCODE_DOWN: return MENU_KEY_DOWN;
    case SDL_SCANCODE_LEFT: return MENU_KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return MENU_KEY_RIGHT;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return MENU_KEY_OK;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: return MENU_KEY_BACK;
    default: return -1;
    }
}

/* In the menu the controller follows the Xbox convention, whatever the game mapping: A confirms, B goes back */
int input_button_to_menu(const InputMap *map, int button)
{
    static const int dirs[4] = { MENU_KEY_UP, MENU_KEY_DOWN, MENU_KEY_LEFT, MENU_KEY_RIGHT };
    for (int i = PAD_FN_UP; i <= PAD_FN_RIGHT; i++)
        if (map->button[i] == button)
            return dirs[i];
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return MENU_KEY_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return MENU_KEY_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return MENU_KEY_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return MENU_KEY_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: return MENU_KEY_OK;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_START: return MENU_KEY_BACK;
    default: return -1;
    }
}

int input_stick_to_menu(InputPads *pads, const SDL_Event *ev)
{
    if (ev->type != SDL_EVENT_GAMEPAD_AXIS_MOTION)
        return -1;
    for (int p = 0; p < INPUT_MAX_PADS; p++) {
        if (!pads->pad[p] || SDL_GetGamepadID(pads->pad[p]) != ev->gaxis.which)
            continue;
        uint8_t now = stick_bits(pads->pad[p]), entered = (uint8_t)(now & ~pads->stick[p]);
        pads->stick[p] = now;
        if (entered & PCE_PAD_UP) return MENU_KEY_UP;
        if (entered & PCE_PAD_DOWN) return MENU_KEY_DOWN;
        if (entered & PCE_PAD_LEFT) return MENU_KEY_LEFT;
        if (entered & PCE_PAD_RIGHT) return MENU_KEY_RIGHT;
        return -1;
    }
    return -1;
}
