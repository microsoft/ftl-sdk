#include "ftl.h"
#include "ftl_private.h"

void auto_bw_add_rtt_sample(ftl_stream_configuration_private_t *ftl, int delay_ms) {

	rtt_info_t *rtt = &ftl->media.auto_bw.xmit_quality.rtt;

	os_lock_mutex(&rtt->mutex);

	if (delay_ms > rtt->max) {
		rtt->max = delay_ms;
	}

	if (delay_ms < rtt->min) {
		rtt->min = delay_ms;
	}

	rtt->sum += delay_ms;
	rtt->count++;

	os_unlock_mutex(&rtt->mutex);
}

auto_bw_rtt_compute_avg(ftl_stream_configuration_private_t *ftl) {

}