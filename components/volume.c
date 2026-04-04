
#include <stdio.h>
#include <pulse/pulseaudio.h>

// --- Internal State ---
static char vol_str[8] = "--";
static int vol_muted = 0;
static int init_status = 1;
static pa_mainloop *ml = NULL;
static pa_context *ctx = NULL;
static const char *active_card = NULL;

#define OP_UNREF(op) do { pa_operation *_o = (op); if (_o) pa_operation_unref(_o); } while (0)

// --- PulseAudio Callback Forward Declarations ---
static void context_state_cb(pa_context *c, void *userdata);

// --- PulseAudio Context Management ---
static void teardown(void) {
    if (!ctx) return;
    pa_context_set_state_callback(ctx, NULL, NULL);
    pa_context_set_subscribe_callback(ctx, NULL, NULL);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    ctx = NULL;
    active_card = NULL;
}

static int connect_ctx(const char *card) {
    int ret;
    teardown();
    if (!ml && !(ml = pa_mainloop_new())) return -1;
    if (!(ctx = pa_context_new(pa_mainloop_get_api(ml), "slstatus"))) return -1;
    init_status = -1;
    pa_context_set_state_callback(ctx, context_state_cb, (void *)card);
    if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        pa_context_unref(ctx);
        ctx = NULL;
        return -1;
    }
    while (init_status < 0 && pa_mainloop_iterate(ml, 1, &ret) >= 0);
    if (init_status != 0) {
        teardown();
        return -1;
    }
    active_card = card;
    return 0;
}

// --- PulseAudio Callbacks ---
static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    if (eol > 0 || !i) {
        if (init_status < 0) init_status = 1;
        return;
    }
    snprintf(vol_str, sizeof(vol_str), "%u",
             (unsigned)(100 * pa_cvolume_avg(&i->volume) / PA_VOLUME_NORM));
    vol_muted = i->mute;
    if (init_status < 0) init_status = 0;
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
        OP_UNREF(pa_context_get_sink_info_by_name(c, userdata, sink_info_cb, NULL));
}

static void context_state_cb(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(c, subscribe_cb, userdata);
        OP_UNREF(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL));
        OP_UNREF(pa_context_get_sink_info_by_name(c, userdata, sink_info_cb, NULL));
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        if (init_status < 0) init_status = 1;
        break;
    default:
        break;
    }
}

// --- Data Preparation ---
// Ensures the PulseAudio context is ready and processes events so that
// vol_str and vol_muted are up-to-date. Returns <0 on error.
static int prepare_data(const char *card) {
    int ret, had_events = 0;
    pa_context_state_t s;
    if (!ctx || card != active_card)
        return connect_ctx(card);
    s = pa_context_get_state(ctx);
    if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED)
        return connect_ctx(card);
    // Drain any pending events (non-blocking)
    while (pa_mainloop_iterate(ml, 0, &ret) > 0)
        had_events = 1;
    // If events triggered async sink-info queries, poll briefly for reply
    if (had_events) {
        if (pa_mainloop_prepare(ml, 20000) >= 0 && pa_mainloop_poll(ml) >= 0)
            pa_mainloop_dispatch(ml);
        while (pa_mainloop_iterate(ml, 0, &ret) > 0);
    }
    return 0;
}

// --- Public API ---
// Returns the current volume percentage as a string, or NULL on error.
const char *vol_perc(const char *card) {
    return prepare_data(card) < 0 ? NULL : vol_str;
}

// Returns "MUT" if muted, "VOL" if not, or NULL on error.
const char *vol_mute(const char *card) {
    return prepare_data(card) < 0 ? NULL : (vol_muted ? "MUT" : "VOL");
}
