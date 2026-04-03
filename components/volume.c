#include <stdio.h>

#include <pulse/pulseaudio.h>

static char vol_str[8] = "--";
static int vol_muted = 0;
static int data_ready = 0;

static void context_state_cb(pa_context *c, void *userdata);
static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                         void *userdata);

static int prepare_data(const char *card)
{
  pa_mainloop *ml;
  pa_context *ctx;
  int ret;

  data_ready = 0;

  ml = pa_mainloop_new();
  if (!ml)
    return -1;

  ctx = pa_context_new(pa_mainloop_get_api(ml), "slstatus");
  if (!ctx) {
    pa_mainloop_free(ml);
    return -1;
  }

  pa_context_set_state_callback(ctx, context_state_cb, (void *)card);
  if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return -1;
  }

  /* run the mainloop until data_ready is set or an error occurs */
  while (!data_ready) {
    if (pa_mainloop_iterate(ml, 1, &ret) < 0)
      break;
  }

  pa_context_disconnect(ctx);
  pa_context_unref(ctx);
  pa_mainloop_free(ml);

  return data_ready ? 0 : -1;
}

const char *vol_perc(const char *card) {
  if (prepare_data(card) < 0)
    return NULL;
  return vol_str;
}

const char *vol_mute(const char *card) {
  if (prepare_data(card) < 0)
    return NULL;
  return vol_muted ? "MUT" : "VOL";
}

static void context_state_cb(pa_context *c, void *userdata) {
  switch (pa_context_get_state(c)) {
  case PA_CONTEXT_READY:
    pa_context_get_sink_info_by_name(c, (const char *)userdata,
                                     sink_info_cb, NULL);
    break;
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    data_ready = 1;
    break;
  default:
    break;
  }
}

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
  if (eol > 0 || !i) {
    data_ready = 1;
    return;
  }

  snprintf(vol_str, sizeof(vol_str), "%u",
           (unsigned)(100 * pa_cvolume_avg(&i->volume) / PA_VOLUME_NORM));
  vol_muted = i->mute;
  data_ready = 1;
}
