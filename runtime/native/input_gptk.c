/* SPDX-License-Identifier: GPL-3.0-only */
/* The Switch-position HID ring is the game's original input API. GPTK edits
 * remap logical pad inputs to those same Npad buttons/axes, without injecting
 * keys or changing game code. Menus and gameplay share this one Npad map. */
#define _POSIX_C_SOURCE 200809L
#include "input_gptk.h"
#include "input_ring.h"

#include "nxinput_gptk4.h"
#include "nxinput_gptk4_preinit.h"
#include "nxinput_gptk_live.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct button_action {
    const char *id;
    uint64_t output;
    uint32_t sources;
};

struct vector_action {
    const char *id;
    float source[2][2];
};

static struct button_action button_actions[] = {
    {"game.npad.a", UINT64_C(1) << 0, 0},
    {"game.npad.b", UINT64_C(1) << 1, 0},
    {"game.npad.x", UINT64_C(1) << 2, 0},
    {"game.npad.y", UINT64_C(1) << 3, 0},
    {"game.npad.l3", UINT64_C(1) << 4, 0},
    {"game.npad.r3", UINT64_C(1) << 5, 0},
    {"game.npad.l", UINT64_C(1) << 6, 0},
    {"game.npad.r", UINT64_C(1) << 7, 0},
    {"game.npad.zl", UINT64_C(1) << 8, 0},
    {"game.npad.zr", UINT64_C(1) << 9, 0},
    {"game.npad.start", UINT64_C(1) << 10, 0},
    {"game.npad.select", UINT64_C(1) << 11, 0},
    {"game.npad.left", UINT64_C(1) << 12, 0},
    {"game.npad.up", UINT64_C(1) << 13, 0},
    {"game.npad.right", UINT64_C(1) << 14, 0},
    {"game.npad.down", UINT64_C(1) << 15, 0},
};

static struct vector_action vector_actions[] = {
    {"game.stick.left", {{0.0f, 0.0f}, {0.0f, 0.0f}}},
    {"game.stick.right", {{0.0f, 0.0f}, {0.0f, 0.0f}}},
};

#define T2_BUTTON_DECL(id_) {id_, NXINPUT_GPTK4_V_DIGITAL}
#define T2_VECTOR_DECL(id_) {id_, NXINPUT_GPTK4_V_VECTOR2}
static const nxinput_gptk4_action_decl t2_actions[] = {
    T2_BUTTON_DECL("game.npad.a"), T2_BUTTON_DECL("game.npad.b"),
    T2_BUTTON_DECL("game.npad.x"), T2_BUTTON_DECL("game.npad.y"),
    T2_BUTTON_DECL("game.npad.l3"), T2_BUTTON_DECL("game.npad.r3"),
    T2_BUTTON_DECL("game.npad.l"), T2_BUTTON_DECL("game.npad.r"),
    T2_BUTTON_DECL("game.npad.zl"), T2_BUTTON_DECL("game.npad.zr"),
    T2_BUTTON_DECL("game.npad.start"), T2_BUTTON_DECL("game.npad.select"),
    T2_BUTTON_DECL("game.npad.left"), T2_BUTTON_DECL("game.npad.up"),
    T2_BUTTON_DECL("game.npad.right"), T2_BUTTON_DECL("game.npad.down"),
    T2_VECTOR_DECL("game.stick.left"), T2_VECTOR_DECL("game.stick.right"),
};
#undef T2_BUTTON_DECL
#undef T2_VECTOR_DECL

/* The port proves one unified Npad control plane, not scene state. The V6
 * parser still requires a context before it may consume input; the schema-4
 * gameplay context is the base map and has no menu override by default. */
static const char *const t2_contexts[] = {"gameplay"};
static nxinput_gptk4_preinit_result t2_preinit;
static nxinput_gptk_live t2_live;
static int t2_initialized;
static int t2_live_ready;
static int t2_current_control = -1;
static int t2_trigger_active[2];
static FILE *t2_receipt;
static int t2_receipt_checked;

static const char *t2_button_sink = "adapter.input.npad-ring.button-mask";

static FILE *t2_receipt_file(void) {
    if (t2_receipt || t2_receipt_checked) return t2_receipt;
    t2_receipt_checked = 1;
    const char *path = getenv("NXGPTK_RECEIPT");
    if (path && *path) {
        t2_receipt = fopen(path, "a");
        if (t2_receipt) setvbuf(t2_receipt, NULL, _IOLBF, 0);
    }
    return t2_receipt;
}

static void t2_receipt_line(const char *line) {
    FILE *out = t2_receipt_file();
    if (out) { fputs(line, out); fputc('\n', out); }
}

static const char *t2_control_name(int control) {
    return nxinput_gptk_control_name(control);
}

static int t2_button_sink_fn(void *user, const char *action,
                             int pressed, float value) {
    struct button_action *entry = user;
    (void)value;
    if (!entry || strcmp(entry->id, action) || t2_current_control < 0 ||
        t2_current_control >= (int)NXINPUT_GPTK_CONTROL_COUNT) return -1;
    uint32_t source = UINT32_C(1) << (unsigned)t2_current_control;
    if (pressed) entry->sources |= source;
    else entry->sources &= ~source;
    char line[512];
    snprintf(line, sizeof line,
             "{\"schema\":\"%s\",\"kind\":\"delivery\",\"context\":\"gameplay\","
             "\"context_source\":\"port:unified-npad\",\"control\":\"%s\","
             "\"event\":\"%s\",\"decision\":\"ACTION\",\"action\":\"%s\","
             "\"sink\":\"%s\",\"pressed\":%d,\"delivery_count\":1}",
             nxinput_gptk_event_evidence_schema(),
             t2_control_name(t2_current_control), pressed ? "press" : "release",
             action, t2_button_sink,
             pressed ? 1 : 0);
    t2_receipt_line(line);
    return 0;
}

static int t2_vector_sink_fn(void *user, const char *action,
                             float x, float y) {
    struct vector_action *entry = user;
    if (!entry || strcmp(entry->id, action)) return -1;
    int source;
    if (t2_current_control == (int)NXINPUT_GPTK_LEFT_STICK) source = 0;
    else if (t2_current_control == (int)NXINPUT_GPTK_RIGHT_STICK) source = 1;
    else return -1;
    int changed = entry->source[source][0] != x || entry->source[source][1] != y;
    entry->source[source][0] = x;
    entry->source[source][1] = y;
    /* Record actual changed deliveries, including the return to neutral. The
     * sink still receives every sample; stationary polls need no disk I/O. */
    if (changed && t2_receipt_file()) {
        char line[512];
        snprintf(line, sizeof line,
                 "{\"schema\":\"%s\",\"kind\":\"delivery\",\"context\":\"gameplay\","
                 "\"context_source\":\"port:unified-npad\",\"control\":\"%s\","
                 "\"event\":\"motion\",\"decision\":\"ACTION\",\"action\":\"%s\","
                 "\"sink\":\"adapter.input.npad-ring.axis-vector\","
                 "\"x\":%.9g,\"y\":%.9g,\"delivery_count\":1}",
                 nxinput_gptk_event_evidence_schema(),
                 t2_control_name(t2_current_control), action,
                 (double)x, (double)y);
        t2_receipt_line(line);
    }
    return 0;
}

static int t2_register_sinks(void) {
    for (size_t i = 0; i < sizeof button_actions / sizeof button_actions[0]; ++i)
        if (nxinput_gptk_live_register(&t2_live, button_actions[i].id,
                t2_button_sink_fn, &button_actions[i]) != 0) return -1;
    for (size_t i = 0; i < sizeof vector_actions / sizeof vector_actions[0]; ++i)
        if (nxinput_gptk_live_register_vector(&t2_live, vector_actions[i].id,
                t2_vector_sink_fn, &vector_actions[i]) != 0) return -1;
    return 0;
}

int t2_input_gptk_initialize(const char *game_directory) {
    if (t2_initialized) return 0;
    t2_initialized = 1;
    nxinput_gptk4_contract contract;
    memset(&contract, 0, sizeof contract);
    contract.actions = t2_actions;
    contract.count = sizeof t2_actions / sizeof t2_actions[0];
    contract.contexts = t2_contexts;
    contract.context_count = sizeof t2_contexts / sizeof t2_contexts[0];
    if (nxinput_gptk4_preinit_load(game_directory, &contract, &t2_preinit) != 0) {
        fprintf(stderr, "[t2/gptk] preinit failed; native Npad controls retained\n");
        return 0;
    }
    fprintf(stderr, "[t2/gptk] %s receipt=%s\n",
            t2_preinit.log_line, t2_preinit.receipt_json);
    t2_receipt_line(t2_preinit.receipt_json);
    if (!t2_preinit.v3.loaded) return 0;

    nxinput_gptk_live_init(&t2_live, &t2_preinit.v3.map);
    if (t2_register_sinks() != 0) {
        fprintf(stderr, "[t2/gptk] sink registration failed; native Npad controls retained\n");
        return 0;
    }
    char error[160];
    if (nxinput_gptk_live_seal(&t2_live, error, sizeof error) != 0) {
        fprintf(stderr, "[t2/gptk] seal refused: %s; native Npad controls retained\n", error);
        return 0;
    }
    if (nxinput_gptk_live_set_context(&t2_live, NXINPUT_GPTK_CONTEXT_GAMEPLAY,
                                      "port:unified-npad") != 0) {
        fprintf(stderr, "[t2/gptk] context refused; native Npad controls retained\n");
        return 0;
    }
    t2_live_ready = 1;
    fprintf(stderr, "[t2/gptk] runtime=%s evidence=%s context=gameplay source=port:unified-npad schema=4\n",
            NXINPUT_GPTK4_RUNTIME_MARKER, nxinput_gptk_event_evidence_schema());
    char line[512];
    snprintf(line, sizeof line,
             "{\"schema\":\"%s\",\"kind\":\"runtime\",\"marker\":\"%s\","
             "\"mapping_sha256\":\"%s\",\"source\":\"%s\",\"gptk_schema\":4,"
             "\"context\":\"gameplay\",\"context_source\":\"port:unified-npad\",\"sinks\":%zu}",
             nxinput_gptk_event_evidence_schema(), NXINPUT_GPTK4_RUNTIME_MARKER,
             t2_preinit.v3.receipt.selected_sha256,
             t2_preinit.bridge.source == NXINPUT_GPTK4_SRC_OWNER ? "owner" : "default",
             sizeof button_actions / sizeof button_actions[0] +
                 sizeof vector_actions / sizeof vector_actions[0]);
    t2_receipt_line(line);
    return 0;
}

static float t2_axis_unit(int32_t value) {
    if (value < 0) return (float)value / 32768.0f;
    return (float)value / 32767.0f;
}

static int16_t t2_axis_sample(float value) {
    if (!isfinite(value)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    long scaled = lroundf(value * (value < 0.0f ? 32768.0f : 32767.0f));
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return (int16_t)scaled;
}

static int t2_trigger_update(unsigned side, float value) {
    const nxinput_gptk4_trigger *trigger = side == 0
        ? &t2_preinit.map4.l2 : &t2_preinit.map4.r2;
    if (!isfinite(value) || value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (t2_trigger_active[side]) {
        if (value <= trigger->exit) t2_trigger_active[side] = 0;
    } else if (value >= trigger->enter) {
        t2_trigger_active[side] = 1;
    }
    return t2_trigger_active[side];
}

static uint64_t t2_action_button_mask(void) {
    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof button_actions / sizeof button_actions[0]; ++i)
        if (button_actions[i].sources) mask |= button_actions[i].output;
    return mask;
}

static int16_t t2_action_axis(const nxinput_gptk4_binding *binding, int axis) {
    if (!binding || binding->kind != NXINPUT_GPTK4_ACTION) return 0;
    for (size_t i = 0; i < sizeof vector_actions / sizeof vector_actions[0]; ++i) {
        struct vector_action *entry = &vector_actions[i];
        if (strcmp(entry->id, binding->action)) continue;
        float chosen_x = 0.0f, chosen_y = 0.0f;
        float chosen_r = -1.0f;
        for (unsigned source = 0; source < 2; ++source) {
            float x = entry->source[source][0], y = entry->source[source][1];
            float radius = x*x + y*y;
            if (radius > chosen_r) { chosen_r = radius; chosen_x = x; chosen_y = y; }
        }
        return t2_axis_sample(axis == 0 ? chosen_x : chosen_y);
    }
    return 0;
}

int t2_input_gptk_route(struct hid_sample *sample, float left_trigger,
                        float right_trigger) {
    if (!sample || !t2_live_ready) return 0;
    static const struct { int control; uint64_t bit; } button_controls[] = {
        {NXINPUT_GPTK_A, UINT64_C(1) << 1}, /* south -> Switch B */
        {NXINPUT_GPTK_B, UINT64_C(1) << 0}, /* east -> Switch A */
        {NXINPUT_GPTK_X, UINT64_C(1) << 2}, /* west -> Switch Y */
        {NXINPUT_GPTK_Y, UINT64_C(1) << 3}, /* north -> Switch X */
        {NXINPUT_GPTK_L1, UINT64_C(1) << 6},
        {NXINPUT_GPTK_R1, UINT64_C(1) << 7},
        {NXINPUT_GPTK_L2, UINT64_C(1) << 8},
        {NXINPUT_GPTK_R2, UINT64_C(1) << 9},
        {NXINPUT_GPTK_L3, UINT64_C(1) << 4},
        {NXINPUT_GPTK_R3, UINT64_C(1) << 5},
        {NXINPUT_GPTK_START, UINT64_C(1) << 10},
        {NXINPUT_GPTK_SELECT, UINT64_C(1) << 11},
        {NXINPUT_GPTK_UP, UINT64_C(1) << 13},
        {NXINPUT_GPTK_DOWN, UINT64_C(1) << 15},
        {NXINPUT_GPTK_LEFT, UINT64_C(1) << 12},
        {NXINPUT_GPTK_RIGHT, UINT64_C(1) << 14},
    };
    uint64_t native_buttons = sample->buttons;

    for (unsigned stick = 0; stick < 2; ++stick) {
        int control = stick == 0 ? NXINPUT_GPTK_LEFT_STICK : NXINPUT_GPTK_RIGHT_STICK;
        int32_t *x = stick == 0 ? &sample->lx : &sample->rx;
        int32_t *y = stick == 0 ? &sample->ly : &sample->ry;
        t2_current_control = control;
        float fx = t2_axis_unit(*x), fy = t2_axis_unit(*y);
        (void)nxinput_gptk_live_feed_vector(&t2_live, control, fx, fy);
        if (nxinput_gptk_live_is_fatal(&t2_live)) return -1;
        if (nxinput_gptk_live_should_consume(&t2_live, control)) {
            nxinput_gptk4_control slot = stick == 0 ? NXINPUT_GPTK4_LS_VECTOR
                                                    : NXINPUT_GPTK4_RS_VECTOR;
            const nxinput_gptk4_binding *binding = nxinput_gptk4_resolve(
                &t2_preinit.map4, "gameplay", slot);
            *x = t2_action_axis(binding, 0);
            *y = t2_action_axis(binding, 1);
        }
    }

    for (size_t i = 0; i < sizeof button_controls / sizeof button_controls[0]; ++i) {
        int pressed = (sample->buttons & button_controls[i].bit) != 0;
        int control = button_controls[i].control;
        if (control == NXINPUT_GPTK_L2)
            pressed = t2_trigger_update(0, left_trigger);
        else if (control == NXINPUT_GPTK_R2)
            pressed = t2_trigger_update(1, right_trigger);
        t2_current_control = control;
        (void)nxinput_gptk_live_feed(&t2_live, control, pressed,
                                     pressed ? 1.0f : 0.0f);
        if (nxinput_gptk_live_is_fatal(&t2_live)) return -1;
        if (nxinput_gptk_live_should_consume(&t2_live, control))
            native_buttons &= ~button_controls[i].bit;
    }
    t2_current_control = -1;
    sample->buttons = native_buttons | t2_action_button_mask();
    return 0;
}

int t2_input_gptk_ready(void) { return t2_live_ready; }

int t2_input_gptk_release_all(void) {
    if(!t2_live_ready)return 0;
    /* Feed real releases through each registered sink while the source control
     * is still known, then close the canonical runtime context. */
    struct hid_sample neutral={0};
    if(t2_input_gptk_route(&neutral,0.0f,0.0f))return -1;
    if(nxinput_gptk_live_clear_context_checked(&t2_live))return -1;
    t2_live_ready=0;
    if(t2_receipt)fflush(t2_receipt);
    return 0;
}
