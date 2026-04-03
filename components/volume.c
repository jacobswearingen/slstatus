#include <stdio.h>

#include <pulse/pulseaudio.h>

static char vol_str[8] = "--";
static int vol_muted;
/* -1 = waiting for initial data, 0 = ok, 1 = error */
static int init_status = 1;

static pa_mainloop *ml;
static pa_context *ctx;
static const char *active_card;

static void context_state_cb(pa_context *c, void *userdata);
static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                         uint32_t idx, void *userdata);
static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                         void *userdata);

static void teardown(void)
{
  if (ctx) {
    pa_context_set_state_callback(ctx, NULL, NULL);
    pa_context_set_subscribe_callback(ctx, NULL, NULL);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    ctx = NULL;
  }
  active_card = NULL;
}

static int connect_ctx(const char *card)
{
  int ret;

  teardown();

  if (!ml) {
    ml = pa_mainloop_new();
    if (!ml)
      return -1;
  }

  ctx = pa_context_new(pa_mainloop_get_api(ml), "slstatus");
  if (!ctx)
    return -1;

  init_status = -1;
  pa_context_set_state_callback(ctx, context_state_cb, (void *)card);

  if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
    pa_context_unref(ctx);
    ctx = NULL;
    return -1;
  }

  /* block until initial sink data arrives or an error occurs */
  while (init_status < 0) {
    if (pa_mainloop_iterate(ml, 1, &ret) < 0)
      break;
  }

  if (init_status != 0) {
    teardown();
    return -1;
  }

  active_card = card;
  return 0;
}

static int prepare_data(const char *card)
{
  int ret;
  pa_context_state_t state;

  if (!ctx || card != active_card)
    return connect_ctx(card);

  state = pa_context_get_state(ctx);
  if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
    return connect_ctx(card);

  /* drain any pending subscription events without blocking */
  while (pa_mainloop_iterate(ml, 0, &ret) > 0)
    ;

  return 0;
}

const char *vol_perc(const char *card)
{
  if (prepare_data(card) < 0)
    return NULL;
  return vol_str;
}

const char *vol_mute(const char *card)
{
  if (prepare_data(card) < 0)
    return NULL;
  return vol_muted ? "MUT" : "VOL";
}

static void context_state_cb(pa_context *c, void *userdata)
{
  switch (pa_context_get_state(c)) {
  case PA_CONTEXT_READY: {
    pa_operation *op;
    pa_context_set_subscribe_callback(c, subscribe_cb, userdata);
    op = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
    if (op)
      pa_operation_unref(op);
    op = pa_context_get_sink_info_by_name(c, (const char *)userdata,
                                          sink_info_cb, NULL);
    if (op)
      pa_operation_unref(op);
    break;
  }
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    if (init_status < 0)
      init_status = 1;
    break;
  default:
    break;
  }
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
             uint32_t idx, void *userdata)
{
  if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK) {
    pa_operation *op = pa_context_get_sink_info_by_name(c, (const char *)userdata,
                                                         sink_info_cb, NULL);
    if (op)
      pa_operation_unref(op);
  }
}

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
  if (eol > 0 || !i) {
    if (init_status < 0)
      init_status = 1;
    return;
  }

  snprintf(vol_str, sizeof(vol_str), "%u",
           (unsigned)(100 * pa_cvolume_avg(&i->volume) / PA_VOLUME_NORM));
  vol_muted = i->mute;
  if (init_status < 0)
    init_status = 0;
}
