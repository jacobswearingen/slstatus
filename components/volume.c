#include <string.h>
#include <pulse/pulseaudio.h>

#include "../util.h"

static unsigned vol_pct = 0;
static int vol_muted = 0;
static int init_status = 1;
static pa_mainloop *ml = NULL;
static pa_context *ctx = NULL;
static char active_card[64] = "";

static void context_state_cb(pa_context *c, void *userdata);

static const char *
resolve_name(const char *card)
{
	return (card && *card) ? card : "@DEFAULT_SINK@";
}

static void
unref(pa_operation *o)
{
	if (o)
		pa_operation_unref(o);
}

static void
teardown(void)
{
	if (!ctx)
		return;
	pa_context_set_state_callback(ctx, NULL, NULL);
	pa_context_set_subscribe_callback(ctx, NULL, NULL);
	pa_context_disconnect(ctx);
	pa_context_unref(ctx);
	ctx = NULL;
	active_card[0] = '\0';
}

static int
connect_ctx(const char *card)
{
	int ret;
	const char *name = resolve_name(card);

	teardown();
	if (!ml && !(ml = pa_mainloop_new())) {
		warn("vol_perc: unable to create pa_mainloop");
		return -1;
	}
	if (!(ctx = pa_context_new(pa_mainloop_get_api(ml), "slstatus"))) {
		warn("vol_perc: unable to create pa_context");
		return -1;
	}

	init_status = -1;
	esnprintf(active_card, sizeof(active_card), "%s", name); /* set before connect: callbacks may fire early */
	pa_context_set_state_callback(ctx, context_state_cb, active_card);

	if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		warn("vol_perc: %s", pa_strerror(pa_context_errno(ctx)));
		pa_context_unref(ctx);
		ctx = NULL;
		active_card[0] = '\0';
		return -1;
	}
	while (init_status < 0 && pa_mainloop_iterate(ml, 1, &ret) >= 0)
		;
	if (init_status != 0) {
		warn("vol_perc: %s", pa_strerror(pa_context_errno(ctx)));
		teardown();
		return -1;
	}
	return 0;
}

static void
sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	if (eol > 0 || !i) {
		if (init_status < 0)
			init_status = 1;
		return;
	}
	vol_pct = 100 * pa_cvolume_avg(&i->volume) / PA_VOLUME_NORM;
	vol_muted = i->mute;
	if (init_status < 0)
		init_status = 0;
}

static void
subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
		unref(pa_context_get_sink_info_by_name(c, userdata, sink_info_cb, NULL));
}

static void
context_state_cb(pa_context *c, void *userdata)
{
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
		pa_context_set_subscribe_callback(c, subscribe_cb, userdata);
		unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL));
		unref(pa_context_get_sink_info_by_name(c, userdata, sink_info_cb, NULL));
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		if (init_status < 0)
			init_status = 1;
		break;
	default:
		break;
	}
}

/* connects, subscribes, and drains pending events so vol_pct/vol_muted are current */
static int
prepare_data(const char *card)
{
	int ret, had_events = 0;
	pa_context_state_t s;
	const char *name = resolve_name(card);

	if (!ctx || !active_card[0] || strcmp(name, active_card) != 0)
		return connect_ctx(card);

	s = pa_context_get_state(ctx);
	if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED)
		return connect_ctx(card);

	while (pa_mainloop_iterate(ml, 0, &ret) > 0)
		had_events = 1;

	/* block only if a reply is actually in flight; poll() sleeps, no busy wait */
	if (had_events) {
		if (pa_mainloop_iterate(ml, 1, &ret) < 0) {
			warn("vol_perc: %s", pa_strerror(pa_context_errno(ctx)));
			return -1;
		}
		while (pa_mainloop_iterate(ml, 0, &ret) > 0)
			;
	}
	return 0;
}

const char *
vol_perc(const char *card)
{
	if (prepare_data(card) < 0)
		return NULL;
	return bprintf("%u", vol_pct);
}

const char *
vol_mute(const char *card)
{
	if (prepare_data(card) < 0)
		return NULL;
	return vol_muted ? "MUT" : "VOL";
}

void
vol_cleanup(void)
{
	teardown();
	if (ml) {
		pa_mainloop_free(ml);
		ml = NULL;
	}
}