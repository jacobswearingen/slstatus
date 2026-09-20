/* See LICENSE file for copyright and license details. */
#include <string.h>
#include <pulse/pulseaudio.h>
#include "../util.h"

static pa_threaded_mainloop *loop;
static pa_context *ctx;

static int muted;
static pa_volume_t vol = PA_VOLUME_MUTED;

static void
state_cb(pa_context *c, void *u)
{
	pa_threaded_mainloop_signal(loop, 0);
}

static void
server_info_cb(pa_context *c, const pa_server_info *i, void *u)
{
	if (i && i->default_sink_name)
		esnprintf((char *)u, 64, "%s", i->default_sink_name);
	pa_threaded_mainloop_signal(loop, 0);
}

static void
sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *u)
{
	if (eol == 0 && i) {
		muted = i->mute;
		vol = pa_cvolume_avg(&i->volume);
	}
	pa_threaded_mainloop_signal(loop, 0);
}

/* wait for an operation to complete, then free it (lock must be held) */
static void
finish(pa_operation *op)
{
	if (!op)
		return;
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(loop);
	pa_operation_unref(op);
}

static int
connect_ctx(void)
{
	pa_context_state_t st;

	if (!loop) {
		if (!(loop = pa_threaded_mainloop_new()) ||
		    !(ctx = pa_context_new(pa_threaded_mainloop_get_api(loop), "slstatus")) ||
		    pa_threaded_mainloop_start(loop) < 0)
			return -1;
		pa_context_set_state_callback(ctx, state_cb, NULL);
	}

	pa_threaded_mainloop_lock(loop);

	st = pa_context_get_state(ctx);
	if (st == PA_CONTEXT_READY) {
		pa_threaded_mainloop_unlock(loop);
		return 0;
	}
	if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
		pa_context_unref(ctx);
		ctx = pa_context_new(pa_threaded_mainloop_get_api(loop), "slstatus");
		pa_context_set_state_callback(ctx, state_cb, NULL);
	}
	if (!ctx || pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		pa_threaded_mainloop_unlock(loop);
		return -1;
	}
	while ((st = pa_context_get_state(ctx)) != PA_CONTEXT_READY &&
	       PA_CONTEXT_IS_GOOD(st))
		pa_threaded_mainloop_wait(loop);

	pa_threaded_mainloop_unlock(loop);
	return st == PA_CONTEXT_READY ? 0 : -1;
}

static int
query(const char *card)
{
	char name[64] = "";

	pa_threaded_mainloop_lock(loop);

	if (!card || !*card || strcmp(card, "@DEFAULT_SINK@") == 0)
		finish(pa_context_get_server_info(ctx, server_info_cb, name));
	else
		esnprintf(name, sizeof(name), "%s", card);

	if (name[0])
		finish(pa_context_get_sink_info_by_name(ctx, name, sink_info_cb, NULL));

	pa_threaded_mainloop_unlock(loop);
	return name[0] ? 0 : -1;
}

const char *
vol_perc(const char *card)
{
	if (connect_ctx() < 0 || query(card) < 0)
		return NULL;
	return bprintf("%u", (unsigned)(100 * vol / PA_VOLUME_NORM));
}

const char *
vol_mute(const char *card)
{
	if (connect_ctx() < 0 || query(card) < 0)
		return NULL;
	return muted ? "MUT" : "VOL";
}

void
vol_cleanup(void)
{
	if (!loop)
		return;
	pa_threaded_mainloop_stop(loop);
	pa_context_unref(ctx);
	pa_threaded_mainloop_free(loop);
	loop = NULL;
	ctx = NULL;
}