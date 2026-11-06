/* SPDX-License-Identifier: BSD-3-Clause */
/* Deterministic CLI mock: any legacy device-access path aborts the test. */

#include "keyboard_control.h"
#include "keyboard_readback.h"

#include <stdlib.h>

int get_keyboard(void) { abort(); }
void send_data(int fd, char buffer[]) { (void)fd; (void)buffer; abort(); }
void set_brightness(int fd, BRIGHTNESS_LVL brt) { (void)fd; (void)brt; abort(); }
void light_keymask(int fd, char mask[17]) { (void)fd; (void)mask; abort(); }
void set_lights_on(int fd) { (void)fd; abort(); }
void set_lights_off(int fd) { (void)fd; abort(); }
void set_animation_mode(int fd, ANIMATION_MODE m, ANIMATION_SPEED s)
{
    (void)fd; (void)m; (void)s; abort();
}
void light_single_key_by_id(int fd, unsigned int id)
{
    (void)fd; (void)id; abort();
}

int keyboard_readback_query(KeyboardReadback *state, char *error, size_t error_size)
{
    (void)error; (void)error_size;
    state->brightness = 2;
    state->effect_code = 0x0a;
    state->protocol_profile = 2;
    return 0;
}

const char *keyboard_readback_mode(unsigned int code)
{
    return code == 0x0a ? "wave" : "unknown";
}

int keyboard_readback_enabled(unsigned int code)
{
    return code == 0x0a ? 1 : -1;
}
