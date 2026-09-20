/* See LICENSE file for copyright and license details. */
#include <string.h>

#include <pulse/pulseaudio.h>

#include "../util.h"

static pa_threaded_mainloop *mainloop = NULL;
static pa_context *ctx = NULL;
static int running = 0;

static int sink_muted = 0;
static pa_volume_t sink_volume = PA_VOLUME_MUTED;
static char sink_name[64] = "";

static int
is_default(const char *card)
{
	return !card || !*card || strcmp(card, "@DEFAULT_SINK@") == 0;
}

static void
context_state_cb(pa_context *c, void *userdata)
{
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		pa_threaded_mainloop_signal(mainloop, 0);
		break;
	default:
		break;
	}
}

/* resolves the default sink name */
static void
server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
	if (i && i->default_sink_name)
		esnprintf(sink_name, sizeof(sink_name), "%s", i->default_sink_name);
	pa_threaded_mainloop_signal(mainloop, 0);
}

static void
sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	if (eol == 0 && i) {
		sink_muted = i->mute;
		sink_volume = pa_cvolume_avg(&i->volume);
	}
	pa_threaded_mainloop_signal(mainloop, 0);
}

static int
connect_ctx(void)
{
	pa_context_state_t st;

	if (!mainloop && !(mainloop = pa_threaded_mainloop_new())) {
		warn("vol_perc: unable to create mainloop");
		return -1;
	}
	if (!ctx) {
		ctx = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "slstatus");
		if (!ctx) {
			warn("vol_perc: unable to create context");
			return -1;
		}
		pa_context_set_state_callback(ctx, context_state_cb, NULL);
	}
	if (!running) {
		if (pa_threaded_mainloop_start(mainloop) < 0) {
			warn("vol_perc: unable to start mainloop");
			return -1;
		}
		running = 1;
	}

	pa_threaded_mainloop_lock(mainloop);

	st = pa_context_get_state(ctx);
	if (st != PA_CONTEXT_READY) {
		/* server restarted or connection dropped: reconnect from scratch */
		if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
			pa_context_unref(ctx);
			ctx = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "slstatus");
			pa_context_set_state_callback(ctx, context_state_cb, NULL);
		}
		if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
			warn("vol_perc: %s", pa_strerror(pa_context_errno(ctx)));
			pa_threaded_mainloop_unlock(mainloop);
			return -1;
		}
		while ((st = pa_context_get_state(ctx)) != PA_CONTEXT_READY &&
		       st != PA_CONTEXT_FAILED && st != PA_CONTEXT_TERMINATED)
			pa_threaded_mainloop_wait(mainloop);
	}

	pa_threaded_mainloop_unlock(mainloop);

	if (st != PA_CONTEXT_READY) {
		warn("vol_perc: %s", pa_strerror(pa_context_errno(ctx)));
		return -1;
	}
	return 0;
}

static int
query_sink(const char *card)
{
	pa_operation *o;
	int r = -1;

	pa_threaded_mainloop_lock(mainloop);

	if (is_default(card)) {
		/* resolve "@DEFAULT_SINK@" to a concrete sink name */
		sink_name[0] = '\0';
		o = pa_context_get_server_info(ctx, server_info_cb, NULL);
		if (o) {
			while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
				pa_threaded_mainloop_wait(mainloop);
			pa_operation_unref(o);
		}
		if (!sink_name[0]) {
			pa_threaded_mainloop_unlock(mainloop);
			return -1;
		}
	} else {
		esnprintf(sink_name, sizeof(sink_name), "%s", card);
	}

	o = pa_context_get_sink_info_by_name(ctx, sink_name, sink_info_cb, NULL);
	if (o) {
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait(mainloop);
		pa_operation_unref(o);
		r = 0;
	}

	pa_threaded_mainloop_unlock(mainloop);
	return r;
}

const char *
vol_perc(const char *card)
{
	if (connect_ctx() < 0 || query_sink(card) < 0)
		return NULL;
	return bprintf("%u", (unsigned)(100ULL * sink_volume / PA_VOLUME_NORM));
}

const char *
vol_mute(const char *card)
{
	if (connect_ctx() < 0 || query_sink(card) < 0)
		return NULL;
	return sink_muted ? "MUT" : "VOL";
}

void
vol_cleanup(void)
{
	if (!mainloop)
		return;
	pa_threaded_mainloop_stop(mainloop);
	if (ctx) {
		pa_context_unref(ctx);
		ctx = NULL;
	}
	pa_threaded_mainloop_free(mainloop);
	mainloop = NULL;
	running = 0;
}