/* SPDX-License-Identifier: BSD-3-Clause */
/* HP OMEN Encoder (03f0:3341, mi_02) read-only GetDeviceInfo implementation. */

#include "keyboard_readback.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ENCODER_VENDOR 0x03f0
#define ENCODER_PRODUCT 0x3341
#define HID_PAYLOAD_SIZE 64
#define HID_QUERY_SIZE 65
#define HID_READ_TIMEOUT_MS 2000
#define HID_MAX_REPLIES 8

/* Exact HID report descriptor observed on the lighting interface (mi_02). */
static const unsigned char lighting_descriptor[] = {
    0x06, 0x13, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15,
    0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x09, 0x02, 0x81, 0x02, 0x09, 0x03, 0x91, 0x02,
    0x0a, 0x00, 0xff, 0xb1, 0x02, 0xc0
};

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
}

static int open_lighting_interface(void)
{
    for (int index = 0; index < 256; ++index) {
        char path[64];
        struct hidraw_devinfo info;
        struct hidraw_report_descriptor desc;
        int descriptor_size = 0;

        snprintf(path, sizeof(path), "/dev/hidraw%d", index);
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        memset(&info, 0, sizeof(info));
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(lighting_descriptor);
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0 &&
            info.bustype == BUS_USB &&
            info.vendor == ENCODER_VENDOR && info.product == ENCODER_PRODUCT &&
            ioctl(fd, HIDIOCGRDESCSIZE, &descriptor_size) >= 0 &&
            descriptor_size == (int)sizeof(lighting_descriptor) &&
            ioctl(fd, HIDIOCGRDESC, &desc) >= 0 &&
            desc.size == sizeof(lighting_descriptor) &&
            memcmp(desc.value, lighting_descriptor, sizeof(lighting_descriptor)) == 0)
            return fd;

        close(fd);
    }
    errno = ENODEV;
    return -1;
}

int keyboard_readback_parse(const unsigned char *reply, size_t length,
                            KeyboardReadback *result)
{
    if (!reply || !result) {
        errno = EINVAL;
        return -1;
    }
    if (length != HID_PAYLOAD_SIZE || reply[0] != 0x80 || reply[1] != 0x01 ||
        (reply[4] == 0xec && reply[5] == 0xac) || reply[12] > 3) {
        errno = EPROTO;
        return -1;
    }
    /* Only Woody2 format/profile 2 was validated with the HP SDK parser. */
    if (reply[10] != 2) {
        errno = EOPNOTSUPP;
        return -1;
    }

    result->protocol_profile = reply[10];
    result->effect_code = reply[11];
    result->brightness = 3 - reply[12];
    return 0;
}

const char *keyboard_readback_mode(unsigned int effect_code)
{
    switch (effect_code) {
    case 0x00: return "off";
    case 0x01: return "static";
    case 0x02: return "breathe";
    case 0x09: return "ripple";
    case 0x0a: return "wave";
    default: return "unknown";
    }
}

int keyboard_readback_enabled(unsigned int effect_code)
{
    if (effect_code == 0x00)
        return 0;
    if (effect_code == 0x01 || effect_code == 0x02 ||
        effect_code == 0x09 || effect_code == 0x0a)
        return 1;
    return -1;
}

static int remaining_ms(const struct timespec *started)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    int64_t elapsed = (int64_t)(now.tv_sec - started->tv_sec) * 1000 +
                      (now.tv_nsec - started->tv_nsec) / 1000000;
    return elapsed >= HID_READ_TIMEOUT_MS ? 0 : HID_READ_TIMEOUT_MS - (int)elapsed;
}

int keyboard_readback_query(KeyboardReadback *result, char *error, size_t error_size)
{
    if (!result) {
        errno = EINVAL;
        set_error(error, error_size, "Invalid status destination");
        return -1;
    }

    int fd = open_lighting_interface();
    if (fd < 0) {
        set_error(error, error_size,
                  "HP OMEN Encoder lighting interface not found or access denied");
        return -1;
    }

    /* hidraw Output report: implicit report number 0 + 64-byte vendor data. */
    const unsigned char query[HID_QUERY_SIZE] = {0x00, 0x80, 0x01};
    struct timespec started;
    int status = -1;
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
        set_error(error, error_size, "Cannot start HID read deadline");
        goto done;
    }
    if (write(fd, query, sizeof(query)) != (ssize_t)sizeof(query)) {
        set_error(error, error_size, "HID GetDeviceInfo write failed or was incomplete");
        goto done;
    }

    for (int received = 0; received < HID_MAX_REPLIES;) {
        int wait_ms = remaining_ms(&started);
        if (wait_ms < 0) {
            set_error(error, error_size, "Cannot measure HID read deadline");
            goto done;
        }
        if (wait_ms == 0) {
            errno = ETIMEDOUT;
            set_error(error, error_size, "HID GetDeviceInfo timed out");
            goto done;
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int ready = poll(&pfd, 1, wait_ms);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0) {
            set_error(error, error_size, "HID GetDeviceInfo poll failed");
            goto done;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            set_error(error, error_size, "HID GetDeviceInfo timed out");
            goto done;
        }
        if (!(pfd.revents & POLLIN)) {
            errno = EIO;
            set_error(error, error_size, "HID GetDeviceInfo device I/O error");
            goto done;
        }

        unsigned char reply[HID_PAYLOAD_SIZE];
        ssize_t count = read(fd, reply, sizeof(reply));
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count < 0) {
            set_error(error, error_size, "HID GetDeviceInfo read failed");
            goto done;
        }
        ++received;
        if (count != HID_PAYLOAD_SIZE) {
            errno = EPROTO;
            set_error(error, error_size, "Unexpected HID GetDeviceInfo reply length");
            goto done;
        }
        if (reply[0] != 0x80 || reply[1] != 0x01)
            continue; /* Ignore an unrelated report, within the bounded limit. */
        if (keyboard_readback_parse(reply, sizeof(reply), result) != 0) {
            set_error(error, error_size,
                      "Unsupported or malformed HP OMEN Encoder device-info response");
            goto done;
        }
        status = 0;
        goto done;
    }
    errno = EPROTO;
    set_error(error, error_size, "No matching HID GetDeviceInfo reply received");

done:
    close(fd);
    return status;
}
