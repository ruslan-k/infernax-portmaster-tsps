/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef T2_INPUT_GPTK_H
#define T2_INPUT_GPTK_H

struct hid_sample;

/* Load the owner's editable NEXTOS_CONTROLLERS/4 map before SDL starts, then
 * route the SDL-normalized Switch-position sample through the pinned V6
 * nxinput live runtime. Returns 0 for an active or safe native fallback. */
int t2_input_gptk_initialize(const char *game_directory);
/* Trigger axes remain analog until GPTK applies the owner's digital
 * enter/exit thresholds. Returns -1 only if an ACK sink fails. */
int t2_input_gptk_route(struct hid_sample *sample, float left_trigger,
                        float right_trigger);
int t2_input_gptk_ready(void);
int t2_input_gptk_release_all(void);

#endif
