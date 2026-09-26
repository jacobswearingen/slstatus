/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slstatus.h"
#include "../util.h"

/* PipeWire/WirePlumber volume, via wpctl(1).
 *
 * `wpctl get-volume ID` reports level and mute state in one line:
 *     Volume: 0.20              (unmuted)
 *     Volume: 1.00 [MUTED]      (muted)
 *
 * A single shell-out therefore yields both values, which keeps this
 * component small: no threads, no subscription, no SPA pod parsing. */

static int
get_volume(const char *sink, unsigned *perc, int *muted)
{
	char *p, *q, *end;
	char cmd[192];
	char line[256];
	FILE *fp;
	double d;

	if (!sink || !*sink)
		sink = "@DEFAULT_AUDIO_SINK@";

	snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", sink);
	if (!(fp = popen(cmd, "r"))) {
		warn("popen '%s':", cmd);
		return -1;
	}

	p = fgets(line, sizeof(line) - 1, fp);
	if (pclose(fp) < 0)
		warn("pclose '%s':", cmd);
	if (!p)
		return -1;

	q = strstr(line, "Volume:");
	if (!q)
		return -1;
	q += sizeof("Volume:") - 1;

	d = strtod(q, &end);
	if (end == q)
		return -1;

	*perc = (unsigned)(d * 100.0 + 0.5);
	*muted = strstr(q, "MUTED") != NULL;
	return 0;
}

const char *
vol(const char *sink)
{
	unsigned perc;
	int muted;

	if (get_volume(sink, &perc, &muted) < 0)
		return NULL;
	return bprintf("%s %u", muted ? "MUT" : "VOL", perc);
}