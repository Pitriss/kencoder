/* SPDX-License-Identifier: BSD-3-Clause */
/* Pure parser tests. No /dev/hidraw device is accessed. */

#include "keyboard_readback.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void verify(unsigned char *report, unsigned char effect,
                   unsigned char raw_brightness, const char *mode, int enabled)
{
    KeyboardReadback result = {0};
    report[11] = effect;
    report[12] = raw_brightness;
    assert(keyboard_readback_parse(report, 64, &result) == 0);
    assert(result.protocol_profile == 2);
    assert(result.effect_code == effect);
    assert(result.brightness == 3 - raw_brightness);
    assert(strcmp(keyboard_readback_mode(result.effect_code), mode) == 0);
    assert(keyboard_readback_enabled(result.effect_code) == enabled);
}

int main(void)
{
    unsigned char reply[64] = {
        [0] = 0x80, [1] = 0x01, [2] = 0x29, [3] = 0xd0,
        [4] = 0x01, [5] = 0x23, [8] = 0x01, [9] = 0x11,
        [10] = 0x02, [11] = 0x01, [12] = 0x02
    };
    KeyboardReadback result = {0};

    verify(reply, 0x00, 0x00, "off", 0);
    verify(reply, 0x01, 0x03, "static", 1); /* Static at brightness 0 != Off. */
    verify(reply, 0x01, 0x02, "static", 1);
    verify(reply, 0x02, 0x01, "breathe", 1);
    verify(reply, 0x09, 0x01, "ripple", 1);
    verify(reply, 0x0a, 0x01, "wave", 1);  /* Direction intentionally unknown. */
    verify(reply, 0x7f, 0x01, "unknown", -1);

    reply[11] = 0x01;
    reply[12] = 0x02;
    assert(keyboard_readback_parse(reply, 63, &result) != 0);
    assert(errno == EPROTO);
    reply[0] = 0x03;
    assert(keyboard_readback_parse(reply, 64, &result) != 0);
    assert(errno == EPROTO);
    reply[0] = 0x80;
    reply[10] = 1;
    assert(keyboard_readback_parse(reply, 64, &result) != 0);
    assert(errno == EOPNOTSUPP);
    reply[10] = 2;
    reply[12] = 4;
    assert(keyboard_readback_parse(reply, 64, &result) != 0);
    assert(errno == EPROTO);
    reply[12] = 2;
    reply[4] = 0xec;
    reply[5] = 0xac;
    assert(keyboard_readback_parse(reply, 64, &result) != 0);
    assert(errno == EPROTO);
    assert(keyboard_readback_parse(NULL, 64, &result) != 0);
    assert(errno == EINVAL);
    assert(keyboard_readback_parse(reply, 64, NULL) != 0);
    assert(errno == EINVAL);

    puts("PASS: device-info parser (validated modes, brightness, unknowns, malformed replies)");
    return 0;
}
