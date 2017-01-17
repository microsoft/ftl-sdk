#include "ftl.h"
#include "ftl_private.h"

#define MAX_RTT_FACTOR 1.3

OS_THREAD_ROUTINE send_thread(void *data);
OS_THREAD_ROUTINE recv_thread(void *data);
OS_THREAD_ROUTINE ping_thread(void *data);
ftl_status_t _internal_media_destroy(ftl_stream_configuration_private_t *ftl);
static int _nack_init(ftl_media_component_common_t *media);
static int _nack_destroy(ftl_media_component_common_t *media);
static ftl_media_component_common_t *_media_lookup(ftl_stream_configuration_private_t *ftl, uint32_t ssrc);
static int _media_make_video_rtp_packet(ftl_stream_configuration_private_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt);
static int _media_make_audio_rtp_packet(ftl_stream_configuration_private_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len);
static int _media_set_marker_bit(ftl_media_component_common_t *mc, uint8_t *in);
static int _media_send_packet(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc);
static int _media_send_slot(ftl_stream_configuration_private_t *ftl, nack_slot_t *slot);
static nack_slot_t* _media_get_empty_slot(ftl_stream_configuration_private_t *ftl, uint32_t ssrc, uint16_t sn);
static float _media_get_queue_fullness(ftl_stream_configuration_private_t *ftl, uint32_t ssrc);
void _update_timestamp(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, int64_t dts_usec);
static void _update_xmit_level(ftl_stream_configuration_private_t *ftl, int *transmit_level, struct timeval *start_tv, int bytes_per_ms);

void _clear_stats(media_stats_t *stats);
static int _update_stats(ftl_stream_configuration_private_t *ftl);
static int _send_pkt_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms);
static int _send_video_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms);

ftl_status_t media_init(ftl_stream_configuration_private_t *ftl) {

	ftl_media_config_t *media = &ftl->media;
	struct hostent *server = NULL;
	ftl_status_t status = FTL_SUCCESS;
	int idx;

	do {
		os_init_mutex(&media->mutex);

		//Create a socket
		if ((media->media_socket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		{
			FTL_LOG(ftl, FTL_LOG_ERROR, "Could not create socket : %s", get_socket_error());
			status = FTL_INTERNAL_ERROR;
			break;
		}
		
		set_socket_send_buf(media->media_socket, 2048);

		FTL_LOG(ftl, FTL_LOG_INFO, "Socket created\n");

		if ((server = gethostbyname(ftl->ingest_ip)) == NULL) {
			FTL_LOG(ftl, FTL_LOG_ERROR, "No such host as %s\n", ftl->ingest_ip);
			status = FTL_DNS_FAILURE;
			break;
		}

		//Prepare the sockaddr_in structure
		media->server_addr.sin_family = AF_INET;
		memcpy((char *)&media->server_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
		media->server_addr.sin_port = htons(media->assigned_port);

		media->max_mtu = MAX_MTU;
		gettimeofday(&media->stats_tv, NULL);

		ftl_media_component_common_t *media_comp[] = { &ftl->video.media_component, &ftl->audio.media_component };
		ftl_media_component_common_t *comp;

		for (idx = 0; idx < sizeof(media_comp) / sizeof(media_comp[0]); idx++) {

			comp = media_comp[idx];

			comp->nack_slots_initalized = FALSE;

			if ((status = _nack_init(comp)) != FTL_SUCCESS) {
				goto cleanup;
			}

			comp->timestamp = 0; //TODO: should start at a random value
			comp->producer = 0;
			comp->consumer = 0;

			_clear_stats(&comp->stats);
		}

		ftl->video.media_component.timestamp_clock = VIDEO_RTP_TS_CLOCK_HZ;
		ftl->audio.media_component.timestamp_clock = AUDIO_SAMPLE_RATE;
		ftl->video.media_component.prev_dts_usec = -1;
		ftl->audio.media_component.prev_dts_usec = -1;

		ftl->video.wait_for_idr_frame = TRUE;

		if ((os_create_thread(&media->recv_thread, NULL, recv_thread, ftl)) != 0) {
			status = FTL_MALLOC_FAILURE;
			break;
		}

		comp = &ftl->video.media_component;

		if (os_semaphore_create(&comp->pkt_ready, "/VideoPkt", O_CREAT, 0) < 0) {
			status = FTL_MALLOC_FAILURE;
			break;
		}

		if ((os_create_thread(&media->send_thread, NULL, send_thread, ftl)) != 0) {
			status = FTL_MALLOC_FAILURE;
			break;
		}

		if ((os_create_thread(&media->ping_thread, NULL, ping_thread, ftl)) != 0) {
			status = FTL_MALLOC_FAILURE;
			break;
		}

		ftl_set_state(ftl, FTL_MEDIA_READY);

		return FTL_SUCCESS;
	} while (0);
cleanup:

	_internal_media_destroy(ftl);

	return status;
}

int media_enable_nack(ftl_stream_configuration_private_t *ftl, uint32_t ssrc, BOOL enabled) {
	ftl_media_component_common_t *mc = NULL;
	if ((mc = _media_lookup(ftl, ssrc)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	mc->nack_enabled = enabled;

	return 0;	
}

ftl_status_t _internal_media_destroy(ftl_stream_configuration_private_t *ftl) {
	ftl_media_config_t *media = &ftl->media;
	ftl_status_t status = FTL_SUCCESS;

	//close while socket still active
	if (ftl_get_state(ftl, FTL_PING_THRD)) {
		ftl_clear_state(ftl, FTL_PING_THRD);
		os_wait_thread(media->ping_thread);
		os_destroy_thread(media->ping_thread);
	}

	//close while socket still active
	if (ftl_get_state(ftl, FTL_TX_THRD)) {
		ftl_clear_state(ftl, FTL_TX_THRD);
		os_semaphore_post(&ftl->video.media_component.pkt_ready);
		os_wait_thread(media->send_thread);
		os_destroy_thread(media->send_thread);
	}

	if (ftl_get_state(ftl, FTL_RX_THRD)) {
		ftl_clear_state(ftl, FTL_RX_THRD);
		//shutdown will cause recv to return with an error so we can exit the thread
		shutdown_socket(media->media_socket, SD_BOTH);
		close_socket(media->media_socket);
		os_wait_thread(media->recv_thread);
		os_destroy_thread(media->recv_thread);
		media->media_socket = 0;
	}

	if (media->media_socket > 0) {
		shutdown_socket(media->media_socket, SD_BOTH);
		media->media_socket = -1;
	}

	os_semaphore_delete(&ftl->video.media_component.pkt_ready);

	os_delete_mutex(&media->mutex);

	media->max_mtu = 0;

	ftl_media_component_common_t *video_comp = &ftl->video.media_component;

	_nack_destroy(video_comp);

	ftl_media_component_common_t *audio_comp = &ftl->audio.media_component;

	_nack_destroy(audio_comp);

	return status;

}

ftl_status_t media_destroy(ftl_stream_configuration_private_t *ftl) {

	if (!ftl_get_state(ftl, FTL_MEDIA_READY)) {
		return FTL_SUCCESS;
	}

	ftl_clear_state(ftl, FTL_MEDIA_READY);

	return _internal_media_destroy(ftl);
}

static int _nack_init(ftl_media_component_common_t *media) {

	int i;
	for (i = 0; i < NACK_RB_SIZE; i++) {
		if ((media->nack_slots[i] = (nack_slot_t *)malloc(sizeof(nack_slot_t))) == NULL) {
			return FTL_MALLOC_FAILURE;
		}

		nack_slot_t *slot = media->nack_slots[i];

		os_init_mutex(&slot->mutex);

		slot->len = 0;
		slot->sn = -1;
		//slot->insert_time = 0;
	}

	media->nack_slots_initalized = TRUE;
	media->nack_enabled = TRUE;
	media->seq_num = media->xmit_seq_num = 0; //TODO: should start at a random value

	return FTL_SUCCESS;
}

static int _nack_destroy(ftl_media_component_common_t *media) {
	int i;
	for (i = 0; i < NACK_RB_SIZE; i++) {
		if (media->nack_slots[i] != NULL) {
			os_delete_mutex(&media->nack_slots[i]->mutex);
			free(media->nack_slots[i]);
			media->nack_slots[i] = NULL;
		}
	}

	return 0;
}

void _clear_stats(media_stats_t *stats) {
	stats->frames_received = 0;
	stats->frames_sent = 0;
	stats->bw_throttling_count = 0;
	stats->bytes_sent = 0;
	stats->packets_sent = 0;
	stats->late_packets = 0;
	stats->lost_packets = 0;
	stats->nack_requests = 0;
	stats->dropped_frames = 0;
	stats->bytes_queued = 0;
	stats->packets_queued = 0;
	stats->pkt_xmit_delay_max = 0;
	stats->pkt_xmit_delay_min = 10000;
	stats->total_xmit_delay = 0;
	stats->xmit_delay_samples = 0;
	stats->current_frame_size = 0;
	stats->max_frame_size = 0;
	gettimeofday(&stats->start_time, NULL);
}

void _update_timestamp(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, int64_t dts_usec) {
	int64_t delta_usec;
	uint32_t delta_ts;

	if (mc->prev_dts_usec < 0) {
		mc->prev_dts_usec = dts_usec;
	}

	delta_usec = dts_usec - mc->prev_dts_usec;

	if (delta_usec) {

		//convert to percentage of 1 second
		double delta_percent = (double)(delta_usec + 1) / 1000000.f;

		delta_ts = (uint32_t)((double)mc->timestamp_clock * delta_percent);

		mc->timestamp += delta_ts;

		mc->prev_dts_usec = dts_usec;
	}
}

int media_speed_test(ftl_stream_configuration_private_t *ftl, int speed_kbps, int duration_ms) {
	ftl_media_component_common_t *mc = &ftl->audio.media_component;
	ftl_media_config_t *media = &ftl->media;
	int64_t bytes_sent = 0;
	int64_t transmit_level = MAX_MTU;
	unsigned char data[MAX_MTU];
	int bytes_per_ms;
	int64_t total_ms = 0;
	struct timeval stop_tv, start_tv, delta_tv;
	float packet_loss = 0.f;
	int64_t ms_elapsed;
	int64_t total_sent = 0;
	int64_t pkts_sent = 0;
	ping_pkt_t *ping;
	nack_slot_t slot;
	uint8_t fmt = 1; //generic nack
	uint8_t ptype = PING_PTYPE; //TODO: made up, need to find something valid
	int wait_retries;
	int initial_rtt, final_rtt;

	if (!ftl_get_state(ftl, FTL_MEDIA_READY)) {
		return 0;
	}

	media_enable_nack(ftl, mc->ssrc, FALSE);
	ftl_clear_state(ftl, FTL_TX_PING_PKTS);


	ping = (ping_pkt_t*)slot.packet;

	int rtp_hdr_len = 0;
	slot.len = sizeof(ping_pkt_t);

	ping->header = htonl((2 << 30) | (fmt << 24) | (ptype << 16) | sizeof(ping_pkt_t));

	/*send ping packet first to get an accurate estimate of rtt under ideal conditions*/
	ftl->media.last_rtt_delay = -1;
	gettimeofday(&ping->xmit_time, NULL);
	_media_send_slot(ftl, &slot);

	wait_retries = 5;
	while (ftl->media.last_rtt_delay < 0 && wait_retries-- > 0) {
		sleep_ms(25);
	};

	initial_rtt = ftl->media.last_rtt_delay;

	int64_t initial_nack_cnt = mc->stats.nack_requests;

	gettimeofday(&start_tv, NULL);

	bytes_per_ms = speed_kbps * 1000 / 8 / 1000;

	while (total_ms < duration_ms) {

		if (transmit_level <= 0) {
			sleep_ms(MAX_MTU / bytes_per_ms + 1);
		}

		gettimeofday(&stop_tv, NULL);
		timeval_subtract(&delta_tv, &stop_tv, &start_tv);
		ms_elapsed = (int64_t)timeval_to_ms(&delta_tv);
		transmit_level += ms_elapsed * bytes_per_ms;
		total_ms += ms_elapsed;

		start_tv = stop_tv;

		while (transmit_level > 0) {
			pkts_sent++;
			bytes_sent = media_send_audio(ftl, 0, data, sizeof(data));
			total_sent += bytes_sent;
			transmit_level -= bytes_sent;
		}
	}

	ftl->media.last_rtt_delay = -1;
	gettimeofday(&ping->xmit_time, NULL);
	_media_send_slot(ftl, &slot);

	wait_retries = 2000 / PING_TX_INTERVAL_MS; // waiting up to 2s for ping to come back
	while (ftl->media.last_rtt_delay < 0 && wait_retries-- > 0) {
		sleep_ms(PING_TX_INTERVAL_MS);
	};

	final_rtt = ftl->media.last_rtt_delay;

	//if we lost a ping packet ignore rtt, if the final rtt is lower than the initial ignore
	if (initial_rtt < 0 || final_rtt < 0 || final_rtt < initial_rtt) {
		initial_rtt = final_rtt = 0;
	}

	//if we didnt get the last ping packet assume the worst for rtt
	if (wait_retries <= 0) {
		initial_rtt = 0;
		final_rtt = 2000;
	}

	FTL_LOG(ftl, FTL_LOG_ERROR, "Sent %d bytes in %d ms (%3.2f kbps) lost %d packets (first rtt: %d, last %d). Estimated peak bitrate %d kbps\n", total_sent, total_ms, (float)total_sent * 8.f * 1000.f / (float)total_ms, mc->stats.nack_requests - initial_nack_cnt, initial_rtt, final_rtt, total_sent * 8 / (total_ms + final_rtt - initial_rtt));

	int lost_pkts = mc->stats.nack_requests - initial_nack_cnt;
	float pkt_loss_percent = (float)lost_pkts / (float)pkts_sent;

	float adjusted_bytes_sent = (float)total_sent * (1.f-pkt_loss_percent);
	int actual_send_time = total_ms + final_rtt - initial_rtt;
	int effective_kbps = adjusted_bytes_sent * 8.f * 1000.f / actual_send_time;

	media_enable_nack(ftl, mc->ssrc, TRUE);
	ftl_set_state(ftl, FTL_TX_PING_PKTS);


	return effective_kbps;
}

int media_send_audio(ftl_stream_configuration_private_t *ftl, int64_t dts_usec, uint8_t *data, int32_t len) {
	ftl_media_component_common_t *mc = &ftl->audio.media_component;
	uint8_t nalu_type = 0;
	int bytes_sent = 0;

	int pkt_len;
	int payload_size;
	int consumed = 0;
	nack_slot_t *slot;
	int remaining = len;
	int retries = 0;

	if (!ftl_get_state(ftl, FTL_MEDIA_READY)) {
		return 0;
	}
	
	_update_timestamp(ftl, mc, dts_usec);

	while (remaining > 0) {
		uint16_t sn = mc->seq_num;
		uint32_t ssrc = mc->ssrc;
		uint8_t *pkt_buf;
		
		if ((slot = _media_get_empty_slot(ftl, ssrc, sn)) == NULL) {
			return 0;
		}

		pkt_buf = slot->packet;
		pkt_len = sizeof(slot->packet);

		os_lock_mutex(&slot->mutex);

		payload_size = _media_make_audio_rtp_packet(ftl, data, remaining, pkt_buf, &pkt_len);

		remaining -= payload_size;
		consumed += payload_size;
		data += payload_size;
		bytes_sent += pkt_len;

		slot->len = pkt_len;
		slot->sn = sn;
		gettimeofday(&slot->insert_time, NULL);

		_media_send_packet(ftl, mc);

		os_unlock_mutex(&slot->mutex);
	}

	return bytes_sent;
}

int media_send_video(ftl_stream_configuration_private_t *ftl, int64_t dts_usec, uint8_t *data, int32_t len, int end_of_frame) {
	ftl_media_component_common_t *mc = &ftl->video.media_component;
	uint8_t nalu_type = 0;
	uint8_t nri;
	int bytes_queued = 0;
	int pkt_len;
	int payload_size;
	int consumed = 0;
	nack_slot_t *slot;
	int remaining = len;
	int first_fu = 1;

	if (!ftl_get_state(ftl, FTL_MEDIA_READY)) {
		return 0;
	}

	nalu_type = data[0] & 0x1F;
	nri = (data[0] >> 5) & 0x3;

	_update_timestamp(ftl, mc, dts_usec);

	if (ftl->video.wait_for_idr_frame) {
		if (nalu_type == H264_NALU_TYPE_SPS) {
			FTL_LOG(ftl, FTL_LOG_INFO, "Got key frame, continuing (dropped %d frames)\n", mc->stats.dropped_frames);
			ftl->video.wait_for_idr_frame = FALSE;
		}
		else {
			if (end_of_frame) {
				mc->stats.dropped_frames++;
			}
			return bytes_queued;
		}
	}

	if (nalu_type == H264_NALU_TYPE_IDR) {
		mc->tmp_seq_num = mc->seq_num;
	}

	while (remaining > 0) {
		uint16_t sn = mc->seq_num;
		uint32_t ssrc = mc->ssrc;
		uint8_t *pkt_buf;

		if ((slot = _media_get_empty_slot(ftl, ssrc, sn)) == NULL) {
			if (nri) {
				FTL_LOG(ftl, FTL_LOG_INFO, "Video queue full, dropping packets until next key frame\n");
				ftl->video.wait_for_idr_frame = TRUE;
			}
			return bytes_queued;
		}

		os_lock_mutex(&slot->mutex);

		pkt_buf = slot->packet;
		pkt_len = sizeof(slot->packet);
		
		slot->first = 0;
		slot->last = 0;

		payload_size = _media_make_video_rtp_packet(ftl, data, remaining, pkt_buf, &pkt_len, first_fu);

		first_fu = 0;
		remaining -= payload_size;
		consumed += payload_size;
		data += payload_size;
		bytes_queued += pkt_len;

		/*if all data has been consumed set marker bit*/
		if (remaining <= 0 && end_of_frame ) {
			_media_set_marker_bit(mc, pkt_buf);
			slot->last = 1;
		}

		slot->len = pkt_len;
		slot->sn = sn;
		gettimeofday(&slot->insert_time, NULL);

		os_unlock_mutex(&slot->mutex);
		os_semaphore_post(&mc->pkt_ready);

		mc->stats.packets_queued++;
		mc->stats.bytes_queued += pkt_len;
	}

	mc->stats.current_frame_size += len;

	if (end_of_frame) {
		mc->stats.frames_received++;

		if (mc->stats.current_frame_size > mc->stats.max_frame_size) {
			mc->stats.max_frame_size = mc->stats.current_frame_size;
		}

		mc->stats.current_frame_size = 0;
	}

	return bytes_queued;
}

static ftl_media_component_common_t *_media_lookup(ftl_stream_configuration_private_t *ftl, uint32_t ssrc) {
	ftl_media_component_common_t *mc = NULL;

	/*check audio*/
	mc = &ftl->audio.media_component;
	if (mc->ssrc == ssrc) {
		return mc;
	}

	/*check video*/
	mc = &ftl->video.media_component;
	if (mc->ssrc == ssrc) {
		return mc;
	}

	return NULL;
}

static nack_slot_t* _media_get_empty_slot(ftl_stream_configuration_private_t *ftl, uint32_t ssrc, uint16_t sn) {
	ftl_media_component_common_t *mc;

	if ((mc = _media_lookup(ftl, ssrc)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to find ssrc %d\n", ssrc);
		return NULL;
	}

	if ( ((mc->seq_num + 1) % NACK_RB_SIZE) == (mc->xmit_seq_num % NACK_RB_SIZE)) {
		return NULL;
	}

	return mc->nack_slots[sn % NACK_RB_SIZE];
}

static float _media_get_queue_fullness(ftl_stream_configuration_private_t *ftl, uint32_t ssrc) {
	ftl_media_component_common_t *mc;

	if ((mc = _media_lookup(ftl, ssrc)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	int packets_queued;

	if (mc->seq_num >= mc->xmit_seq_num) {
		packets_queued = mc->seq_num - mc->xmit_seq_num;
	}
	else {
		packets_queued = 65535 - mc->xmit_seq_num + mc->seq_num + 1;
	}

	return (float)packets_queued / (float)NACK_RB_SIZE;
}

static int _media_send_slot(ftl_stream_configuration_private_t *ftl, nack_slot_t *slot) {
	int tx_len;
	
	os_lock_mutex(&ftl->media.mutex);
	if ((tx_len = sendto(ftl->media.media_socket, slot->packet, slot->len, 0, (struct sockaddr*) &ftl->media.server_addr, sizeof(struct sockaddr_in))) == SOCKET_ERROR)
	{
		FTL_LOG(ftl, FTL_LOG_ERROR, "sendto() failed with error: %s", get_socket_error());
	}
	os_unlock_mutex(&ftl->media.mutex);

	return tx_len;
}

static int _media_send_packet(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc) {

	int tx_len;

	nack_slot_t *slot = mc->nack_slots[mc->xmit_seq_num % NACK_RB_SIZE];

	if (mc->xmit_seq_num == mc->seq_num) {
		FTL_LOG(ftl, FTL_LOG_INFO, "ERROR: No packets in ring buffer (%d == %d)\n", mc->xmit_seq_num, mc->seq_num);
	}

	os_lock_mutex(&slot->mutex);

	tx_len = _media_send_slot(ftl, slot);

	gettimeofday(&slot->xmit_time, NULL);

	mc->xmit_seq_num++;

	if (slot->last) {
		mc->stats.frames_sent++;
	}
	mc->stats.packets_sent++;
	mc->stats.bytes_sent += tx_len;

	struct timeval profile_delta;
	float xmit_delay_delta;
	timeval_subtract(&profile_delta, &slot->xmit_time, &slot->insert_time);

	xmit_delay_delta = timeval_to_ms(&profile_delta);

	if (xmit_delay_delta > mc->stats.pkt_xmit_delay_max) {
		mc->stats.pkt_xmit_delay_max = (int)xmit_delay_delta;
	}
	else if (xmit_delay_delta < mc->stats.pkt_xmit_delay_min) {
		mc->stats.pkt_xmit_delay_min = (int)xmit_delay_delta;
	}

	mc->stats.total_xmit_delay += (int)xmit_delay_delta;
	mc->stats.xmit_delay_samples++;

	os_unlock_mutex(&slot->mutex);
	
	return tx_len;
}

static int _nack_resend_packet(ftl_stream_configuration_private_t *ftl, uint32_t ssrc, uint16_t sn) {
	ftl_media_component_common_t *mc;
	int tx_len = 0;

	if ((mc = _media_lookup(ftl, ssrc)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	/*map sequence number to slot*/
	nack_slot_t *slot = mc->nack_slots[sn % NACK_RB_SIZE];
	os_lock_mutex(&slot->mutex);

	if (slot->sn != sn) {
		FTL_LOG(ftl, FTL_LOG_WARN, "[%d] expected sn %d in slot but found %d...discarding retransmit request", ssrc, sn, slot->sn);
		os_unlock_mutex(&slot->mutex);
		return 0;
	}

	int req_delay = 0;
	struct timeval delta, now;
	gettimeofday(&now, NULL);
	timeval_subtract(&delta, &now, &slot->xmit_time);
	req_delay = (int)timeval_to_ms(&delta);

	if (mc->nack_enabled) {
		tx_len = _media_send_slot(ftl, slot);
		FTL_LOG(ftl, FTL_LOG_INFO, "[%d] resent sn %d, request delay was %d ms", ssrc, sn, req_delay);
	}
	mc->stats.nack_requests++;

	os_unlock_mutex(&slot->mutex);

	return tx_len;
}

static int _write_rtp_header(uint8_t *buf, size_t len, uint8_t ptype, uint16_t seq_num, uint32_t timestamp, uint32_t ssrc) {
	uint32_t rtp_header;
	
	if (RTP_HEADER_BASE_LEN > len) {
		return -1;
	}

	//TODO need to worry about alignment on some platforms
	uint32_t *out_header = (uint32_t *)buf;

	rtp_header = htonl((2 << 30) | (ptype << 16) | seq_num);

	*out_header++ = rtp_header;
	rtp_header = htonl((uint32_t)timestamp);
	*out_header++ = rtp_header;
	rtp_header = htonl(ssrc);
	*out_header++ = rtp_header;

	return (int)((uint8_t*)out_header - buf);
}

static int _media_make_video_rtp_packet(ftl_stream_configuration_private_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt) {
	uint8_t sbit = 0, ebit = 0;
	int frag_len;
	ftl_video_component_t *video = &ftl->video;
	ftl_media_component_common_t *mc = &video->media_component;
	int rtp_hdr_len = 0;

	if ((rtp_hdr_len = _write_rtp_header(out, *out_len, mc->payload_type, mc->seq_num, mc->timestamp, mc->ssrc)) < 0) {
		return -1;
	}

	out += rtp_hdr_len;

	mc->seq_num++;

	//if this packet can fit into a it's own packet then just use single nalu mode
	if (first_pkt && in_len <= (ftl->media.max_mtu - RTP_HEADER_BASE_LEN)) {
		frag_len = in_len;
		*out_len = frag_len + rtp_hdr_len;
		memcpy(out, in, frag_len);
	}
	else {//otherwise packetize using FU-A

		if (first_pkt) {
			sbit = 1;
			video->fua_nalu_type = in[0];
			in += 1;
			in_len--;
		}
		else if (in_len <= (ftl->media.max_mtu - RTP_HEADER_BASE_LEN - RTP_FUA_HEADER_LEN)) {
			ebit = 1;
		}

		out[0] = (video->fua_nalu_type & 0x60) | 28;
		out[1] = (sbit << 7) | (ebit << 6) | (video->fua_nalu_type & 0x1F);

		out += 2;

		frag_len = ftl->media.max_mtu - rtp_hdr_len - RTP_FUA_HEADER_LEN;

		if (frag_len > in_len) {
			frag_len = in_len;
		}

		memcpy(out, in, frag_len);

		*out_len = frag_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN;
	}

	return frag_len + sbit;
}

static int _media_make_audio_rtp_packet(ftl_stream_configuration_private_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len) {
	int payload_len = in_len;
	
	ftl_audio_component_t *audio = &ftl->audio;
	ftl_media_component_common_t *mc = &audio->media_component;

	int rtp_hdr_len = 0;

	if ((rtp_hdr_len = _write_rtp_header(out, *out_len, mc->payload_type, mc->seq_num, mc->timestamp, mc->ssrc)) < 0) {
		return -1;
	}

	out += rtp_hdr_len;

	mc->seq_num++;

	memcpy(out, in, payload_len);

	*out_len = payload_len + RTP_HEADER_BASE_LEN;

	return in_len;
}

static int _media_set_marker_bit(ftl_media_component_common_t *mc, uint8_t *in) {
	uint32_t rtp_header;

	rtp_header = ntohl(*((uint32_t*)in));
	rtp_header |= 1 << 23; /*set marker bit*/
	*((uint32_t*)in) = htonl(rtp_header);

	return 0;
}


/*handles rtcp packets from ingest including lost packet retransmission requests (nack)*/
OS_THREAD_ROUTINE recv_thread(void *data)
{
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
	ftl_media_config_t *media = &ftl->media;
	int ret;
	unsigned char *buf;
	struct sockaddr_in remote_addr;
	socklen_t addr_len;

#ifdef _WIN32
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
		FTL_LOG(ftl, FTL_LOG_WARN, "Failed to set recv_thread priority to THREAD_PRIORITY_TIME_CRITICAL\n");
	}
#endif

	if ((buf = (unsigned char*)malloc(MAX_PACKET_BUFFER)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to allocate recv buffer\n");
		return (OS_THREAD_TYPE)-1;
	}

	ftl_set_state(ftl, FTL_RX_THRD);

	while (ftl_get_state(ftl, FTL_RX_THRD)) {

		addr_len = sizeof(remote_addr);
		ret = recvfrom(media->media_socket, buf, MAX_PACKET_BUFFER, 0, (struct sockaddr *)&remote_addr, &addr_len);
		if (ret <= 0) {
			continue;
		}

		if (remote_addr.sin_addr.s_addr != inet_addr(ftl->ingest_ip))
		{
			FTL_LOG(ftl, FTL_LOG_WARN, "Discarded packet from unexpected ip: %s\n", inet_ntoa(remote_addr.sin_addr));
			continue;
		}

		int version, padding, feedbackType, ptype, length, ssrcSender, ssrcMedia;
		uint16_t snBase, blp, sn;
		int recv_len = ret;

		if (recv_len < 2) {
			FTL_LOG(ftl, FTL_LOG_WARN, "recv packet too small to parse, discarding\n");
			continue;
		}

		/*extract rtp header*/
		version = (buf[0] >> 6) & 0x3;
		padding = (buf[0] >> 5) & 0x1;
		feedbackType = buf[0] & 0x1F;
		ptype = buf[1];

		if (feedbackType == 1 && ptype == 205) {

			length = ntohs(*((uint16_t*)(buf + 2)));

			if (recv_len < ((length + 1) * 4)) {
				FTL_LOG(ftl, FTL_LOG_WARN, "reported len was %d but packet is only %d...discarding\n", recv_len, ((length + 1) * 4));
				continue;
			}

			ssrcSender = ntohl(*((uint32_t*)(buf + 4)));
			ssrcMedia = ntohl(*((uint32_t*)(buf + 8)));

			uint16_t *p = (uint16_t *)(buf + 12);
			
			int fci;
			for (fci = 0; fci < (length - 2); fci++) {
				//request the first sequence number
				snBase = ntohs(*p++);
				_nack_resend_packet(ftl, ssrcMedia, snBase);
				blp = ntohs(*p++);
				if (blp) {
					int i;
					for (i = 0; i < 16; i++) {
						if ((blp & (1 << i)) != 0) {
							sn = snBase + i + 1;
							_nack_resend_packet(ftl, ssrcMedia, sn);
						}
					}
				}
			}
		}
		else if (feedbackType == 1 && ptype == PING_PTYPE) {

			ping_pkt_t *ping = (ping_pkt_t *)buf;

			struct timeval now;
			int delay_ms;

			gettimeofday(&now, NULL);
			delay_ms = timeval_subtract_to_ms(&now, &ping->xmit_time);

			ftl->media.last_rtt_delay = delay_ms;
		}
	}

	free(buf);

	FTL_LOG(ftl, FTL_LOG_INFO, "Exited Recv Thread\n");

	return (OS_THREAD_TYPE)0;
}

OS_THREAD_ROUTINE send_thread(void *data)
{
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
	ftl_media_config_t *media = &ftl->media;
	ftl_media_component_common_t *video = &ftl->video.media_component;

	int first_packet = 1;
	int bytes_per_ms;
	int pkt_sent;
	int video_kbps = -1;
	int disable_flow_control = 1;
	int initial_peak_kbps;

	int transmit_level;
	struct timeval start_tv;

#ifdef _WIN32
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
		FTL_LOG(ftl, FTL_LOG_WARN, "Failed to set recv_thread priority to THREAD_PRIORITY_TIME_CRITICAL\n");
	}
#endif

	ftl_set_state(ftl, FTL_TX_THRD);

	initial_peak_kbps = video->kbps = video->peak_kbps;
	video_kbps = 0;
	transmit_level = 5 * video->kbps * 1000 / 8 / 1000; /*small initial level to prevent bursting at the start of a stream*/

	while (1) {

		if (initial_peak_kbps != video->peak_kbps) {
			initial_peak_kbps = video->kbps = video->peak_kbps;
		}

		if (video->kbps != video_kbps) {
			bytes_per_ms = video->kbps * 1000 / 8 / 1000;
			video_kbps = video->kbps;

			disable_flow_control = 0;
			if (video_kbps <= 0) {
				disable_flow_control = 1;
			}
		}

		os_semaphore_pend(&video->pkt_ready, FOREVER);

		if (!ftl_get_state(ftl, FTL_TX_THRD)) {
			break;
		}

		if (disable_flow_control) {
			_media_send_packet(ftl, video);
		}
		else {
			pkt_sent = 0;
			if (first_packet) {
				gettimeofday(&start_tv, NULL);
				first_packet = 0;
			}

			_update_xmit_level(ftl, &transmit_level, &start_tv, bytes_per_ms);
			while (!pkt_sent && ftl_get_state(ftl, FTL_TX_THRD)) {

				if (transmit_level <= 0) {
					ftl->video.media_component.stats.bw_throttling_count++;
					sleep_ms(MAX_MTU / bytes_per_ms + 1);
					_update_xmit_level(ftl, &transmit_level, &start_tv, bytes_per_ms);
				}

				if (transmit_level > 0) {
					transmit_level -= _media_send_packet(ftl, video);
					pkt_sent = 1;
				}
			}

		}

		_update_stats(ftl);
	}

	FTL_LOG(ftl, FTL_LOG_INFO, "Exited Send Thread\n");
	return (OS_THREAD_TYPE)0;
}

static void _update_xmit_level(ftl_stream_configuration_private_t *ftl, int *transmit_level, struct timeval *start_tv, int bytes_per_ms) {

	struct timeval stop_tv;

	gettimeofday(&stop_tv, NULL);

	*transmit_level += (int)timeval_subtract_to_ms(&stop_tv, start_tv) * bytes_per_ms;

	if (*transmit_level > (MAX_XMIT_LEVEL_IN_MS * bytes_per_ms)) {
		*transmit_level = MAX_XMIT_LEVEL_IN_MS * bytes_per_ms;
	}

	*start_tv = stop_tv;
}

static int _update_stats(ftl_stream_configuration_private_t *ftl) {
	struct timeval now, delta;
	gettimeofday(&now, NULL);
	timeval_subtract(&delta, &now, &ftl->media.stats_tv);
	float stats_interval = timeval_to_ms(&delta);

	if (stats_interval > 5000) {

		ftl->media.stats_tv = now;

		_send_pkt_stats(ftl, &ftl->video.media_component, stats_interval);
		//_send_pkt_stats(ftl, &ftl->audio.media_component);
		_send_video_stats(ftl, &ftl->video.media_component, stats_interval);

		//_clear_stats(&ftl->video.media_component.stats);
	}

	return 0;
}

static int _send_pkt_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms) {
	ftl_status_msg_t m;
	m.type = FTL_STATUS_VIDEO_PACKETS;

	m.msg.pkt_stats.period = (int)interval_ms;
	m.msg.pkt_stats.sent = mc->stats.packets_sent;
	m.msg.pkt_stats.nack_reqs = mc->stats.nack_requests;
	m.msg.pkt_stats.lost = 0; // needs rtcp reports to get this value
	m.msg.pkt_stats.recovered; // need rtcp reports to get this value
	m.msg.pkt_stats.late; // need rtcp reports to get this value
	m.msg.pkt_stats.min_xmit_delay = mc->stats.pkt_xmit_delay_min;
	m.msg.pkt_stats.max_xmit_delay = mc->stats.pkt_xmit_delay_max;
	m.msg.pkt_stats.avg_xmit_delay = mc->stats.total_xmit_delay / mc->stats.xmit_delay_samples;

	mc->stats.pkt_xmit_delay_max = 0;
	mc->stats.pkt_xmit_delay_min = 10000;
	mc->stats.nack_requests = 0;

	enqueue_status_msg(ftl, &m);

	return 0;
}

static int _send_video_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms) {
	ftl_status_msg_t m;
	ftl_video_frame_stats_msg_t *v = &m.msg.video_stats;
	struct timeval now;
	
	m.type = FTL_STATUS_VIDEO;

	gettimeofday(&now, NULL);
	v->period = timeval_subtract_to_ms(&now, &mc->stats.start_time);

	v->frames_queued = mc->stats.frames_received;
	v->frames_sent = mc->stats.frames_sent;
	v->bw_throttling_count = mc->stats.bw_throttling_count;
	v->bytes_queued = mc->stats.bytes_queued;
	v->bytes_sent = mc->stats.bytes_sent;
	v->queue_fullness = (int)(_media_get_queue_fullness(ftl, mc->ssrc) * 100.f);
	v->max_frame_size = mc->stats.max_frame_size;

	mc->stats.max_frame_size = 0;
	enqueue_status_msg(ftl, &m);

	return 0;
}

OS_THREAD_ROUTINE ping_thread(void *data) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
	ftl_media_config_t *media = &ftl->media;

	ping_pkt_t *ping;
	nack_slot_t slot;
	uint8_t fmt = 1; 
	uint8_t ptype = PING_PTYPE;
	ping = (ping_pkt_t*)slot.packet;

	int rtp_hdr_len = 0;
	slot.len = sizeof(ping_pkt_t);


	//      0                   1                   2                   3
	//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//   |V=2|P|   FMT   |       PT      |          length               |
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	ping->header = htonl((2 << 30) | (fmt << 24) | (ptype << 16) | sizeof(ping_pkt_t));

	ftl_set_state(ftl, FTL_PING_THRD | FTL_TX_PING_PKTS);

	while (ftl_get_state(ftl, FTL_PING_THRD)) {

		sleep_ms(PING_TX_INTERVAL_MS);

		if (!ftl_get_state(ftl, FTL_TX_PING_PKTS)) {
			continue;
		}

		gettimeofday(&ping->xmit_time, NULL);
		_media_send_slot(ftl, &slot);
	}

	FTL_LOG(ftl, FTL_LOG_INFO, "Exited Ping Thread\n");
	
	return 0;
}