/*
Copyright 2020 Taras Zakharchenko

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1.  Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
2.  Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
3.  Neither the name of the copyright holder nor the names of its contributors
    may be used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include "keyboard_control.h"
#include "keyboard_readback.h"

extern char *optarg;
extern int optind, opterr, optopt;

const char help_text[] =
"HELP TEXT\n"
"-s static lightning\n"
"-o turn off lightning\n"
"-h show this help\n"
"-b <0|1|2|3> brightness\n"
"-v <slow|medium|fast> animation speed\n"
"-a <ripple|left_wave|right_wave|breathe> animation mode\n"
"--status read device-reported lighting state (never changes settings)\n"
"--json machine-readable output; requires --status\n"
;

static void print_readback(const KeyboardReadback *state, int json)
{
    const char *mode = keyboard_readback_mode(state->effect_code);
    int enabled = keyboard_readback_enabled(state->effect_code);

    if (json) {
        printf("{\"source\":\"hardware\",\"enabled\":");
        if (enabled < 0)
            printf("null");
        else
            fputs(enabled ? "true" : "false", stdout);
        printf(",\"brightness\":%d,\"mode\":\"%s\","
               "\"direction\":null,\"speed\":null,\"partial\":true,"
               "\"effect_code\":%u}\n", state->brightness, mode, state->effect_code);
    } else {
        printf("Source: hardware\nLighting: %s\nMode: %s\nReported brightness: %d\n"
               "Wave direction: unknown\nAnimation speed: unknown\n",
               enabled < 0 ? "unknown" : enabled ? "on" : "off", mode,
               state->brightness);
    }
}

int main(int argc, char *argv[])
{
    enum { OPT_STATUS = 1000, OPT_JSON };
    static const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"status", no_argument, NULL, OPT_STATUS},
        {"json", no_argument, NULL, OPT_JSON},
        {0, 0, 0, 0}
    };
    int fd;
    int option;
    ANIMATION_SPEED speed = ANIMATION_SPEED_FAST;
    BRIGHTNESS_LVL brightness = BRIGHTNESS_LVL_3;
    ANIMATION_MODE animode;
    int ani_set = 0;
    int off_set = 0;
    int sta_set = 0;
    int need_help = 0;
    int status_set = 0;
    int json_set = 0;
    int setting_option_seen = 0;
    int bad_option = 0;
    int missing_value = 0;
    int last_bad_option = 0;

    speed = ANIMATION_SPEED_SLOW;
    while((option = getopt_long(argc, argv, ":sohb:v:a:", long_options, NULL)) != -1)
    {
        switch(option){
            case OPT_STATUS:
                status_set = 1;
                break;
            case OPT_JSON:
                json_set = 1;
                break;
            case 's':
                setting_option_seen = 1;
                sta_set = 1;
                break;
            case 'o':
                setting_option_seen = 1;
                off_set = 1;
                break;
            case 'h':
                need_help = 1;
                break;
            case 'b':
                setting_option_seen = 1;
                if(strcmp(optarg, "0") == 0)        brightness = BRIGHTNESS_LVL_0;
                else if(strcmp(optarg, "1") == 0)   brightness = BRIGHTNESS_LVL_1;
                else if(strcmp(optarg, "2") == 0)   brightness = BRIGHTNESS_LVL_2;
                else if(strcmp(optarg, "3") == 0)   brightness = BRIGHTNESS_LVL_3;
                break;
            case 'v':
                setting_option_seen = 1;
                if(strcmp(optarg, "slow") == 0)         { speed = ANIMATION_SPEED_SLOW; }
                else if(strcmp(optarg, "medium") == 0)  { speed = ANIMATION_SPEED_MEDIUM; }
                else if(strcmp(optarg, "fast") == 0)    { speed = ANIMATION_SPEED_FAST; }
                break;
            case 'a':
                setting_option_seen = 1;
                if(strcmp(optarg, "ripple") == 0)           { animode = ANIMATION_MODE_RIPPLE; ani_set = 1; }
                else if(strcmp(optarg, "left_wave") == 0)   { animode = ANIMATION_MODE_LEFT_WAVE; ani_set = 1; }
                else if(strcmp(optarg, "right_wave") == 0)  { animode = ANIMATION_MODE_RIGT_WAVE; ani_set = 1; }
                else if(strcmp(optarg, "breathe") == 0)     { animode = ANIMATION_MODE_BREATHE; ani_set = 1; }
                break;
            case ':':
                missing_value = 1;
                break;
            case '?':
                bad_option = 1;
                last_bad_option = optopt;
                break;
        }
    }

    if(need_help)
    {
        printf("%s", help_text);
        return 0;
    }

    if (status_set || json_set) {
        if (!status_set || setting_option_seen || bad_option ||
            missing_value || optind < argc) {
            fprintf(stderr, "Status requires --status [--json] without lighting options\n");
            return 2;
        }
        KeyboardReadback state;
        char error[160] = {0};
        if (keyboard_readback_query(&state, error, sizeof(error)) != 0) {
            fprintf(stderr, "encoder: %s\n", error);
            return 1;
        }
        print_readback(&state, json_set);
        return 0;
    }

    /* Preserve the legacy setter interface and its original diagnostics. */
    if (missing_value)
        printf("Option needs a value\n");
    if (bad_option)
        printf("Unknown option: %c\n", last_bad_option);

    fd = get_keyboard();
    if(fd < 0)
    {
        fprintf(stderr, "OMEN Encoder keyboard not found or access denied\n");
        return 1;
    }

    set_brightness(fd, brightness);
    if(off_set)
    {
        set_lights_off(fd);
    }
    else if(sta_set)
    {
        set_lights_on(fd);
    }
    else if(ani_set)
    {
        set_animation_mode(fd, animode, speed);
    }

    close(fd);
}