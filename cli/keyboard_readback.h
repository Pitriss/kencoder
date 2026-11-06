/* SPDX-License-Identifier: BSD-3-Clause */
/* Read-only HP OMEN Encoder / Woody2 lighting status interface. */
#ifndef KENCODER_KEYBOARD_READBACK_H
#define KENCODER_KEYBOARD_READBACK_H

#include <stddef.h>

typedef struct {
    unsigned int effect_code;
    unsigned int protocol_profile;
    int brightness; /* User scale 0..3, as reported by the device. */
} KeyboardReadback;

/* Pure parser for a 64-byte reply; does not access hardware. */
int keyboard_readback_parse(const unsigned char *reply, size_t length,
                            KeyboardReadback *result);

/* Performs exactly one 65-byte '00 80 01 ...' HID write, then bounded reads.
 * No lighting setters or flash operations are called. Returns 0 on success.
 */
int keyboard_readback_query(KeyboardReadback *result, char *error, size_t error_size);

const char *keyboard_readback_mode(unsigned int effect_code);
/* 0=off, 1=on, -1=unknown. */
int keyboard_readback_enabled(unsigned int effect_code);

#endif
