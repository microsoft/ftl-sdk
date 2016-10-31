#define __FTL_INTERNAL
#include "ftl.h"
#include "ftl_private.h"

#ifdef _WIN32
static DWORD WINAPI recv_thread(LPVOID data);
static DWORD WINAPI send_thread(LPVOID data);
#else
static void *recv_thread(void *data);
static void *send_thread(void *data);
#endif
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
static int _update_stats(ftl_stream_configuration_private_t *ftl);
static int _send_pkt_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms);
static int _send_video_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms);
void _update_timestamp(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, int64_t dts_usec);
void _clear_stats(media_stats_t *stats);

ftl_status_t media_init(ftl_stream_configuration_private_t *ftl) {

	ftl_media_config_t *media = &ftl->media;
	struct hostent *server = NULL;
	ftl_status_t status = FTL_SUCCESS;
	int idx;

	os_init_mutex(&media->mutex);

	//Create a socket
	if ((media->media_socket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		FTL_LOG(ftl, FTL_LOG_ERROR, "Could not create socket : %s", ftl_get_socket_error());
	}

	int peak_kbps = ftl->video.media_component.kbps;

	if (peak_kbps == 0) {
		peak_kbps = PEAK_BITRATE_KBPS;
	}

	int sendbuf = (peak_kbps * 1000 / 8) / (1000 / 10);
	ftl_set_socket_send_buf(media->media_socket, sendbuf);

	FTL_LOG(ftl, FTL_LOG_INFO, "Socket created\n");

	if ((server = gethostbyname(ftl->ingest_ip)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "No such host as %s\n", ftl->ingest_ip);
		return FTL_DNS_FAILURE;
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
			return status;
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

	media->recv_thread_running = TRUE;
	if ((os_create_thread(&media->recv_thread, NULL, recv_thread, ftl)) != 0) {
		return FTL_MALLOC_FAILURE;
	}

	comp = &ftl->video.media_component;

#ifdef _WIN32
	if ((comp->pkt_ready = CreateSemaphore(NULL, 0, 1000000, NULL)) == NULL) {
#else
	if (sem_init(&comp->pkt_ready, 0 /* pshared */, 0 /* value */)) {
#endif
		return FTL_MALLOC_FAILURE;
	}

	media->send_thread_running = TRUE;
	if ((os_create_thread(&media->send_thread, NULL, send_thread, ftl)) != 0) {
		return FTL_MALLOC_FAILURE;
	}

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

ftl_status_t media_destroy(ftl_stream_configuration_private_t *ftl) {
	ftl_media_config_t *media = &ftl->media;
	struct hostent *server = NULL;
	ftl_status_t status = FTL_SUCCESS;


	media->recv_thread_running = FALSE;
#ifdef _WIN32
	ftl_close_socket(media->media_socket);
#else
	shutdown(media->media_socket, SHUT_RDWR);
#endif
	os_wait_thread(media->recv_thread);
	os_destroy_thread(media->recv_thread);

	media->send_thread_running = FALSE;
#ifdef _WIN32
	ReleaseSemaphore(ftl->video.media_component.pkt_ready, 1, NULL); 
	os_wait_thread(media->send_thread);
	os_destroy_thread(media->send_thread);
	CloseHandle(ftl->video.media_component.pkt_ready);
#else
	sem_post(&ftl->video.media_component.pkt_ready);
	os_wait_thread(media->send_thread);
	os_destroy_thread(media->send_thread);
	sem_destroy(&ftl->video.media_component.pkt_ready);
#endif
	os_delete_mutex(&media->mutex);

	media->max_mtu = 0;

	ftl_media_component_common_t *video_comp = &ftl->video.media_component;

	_nack_destroy(video_comp);

	ftl_media_component_common_t *audio_comp = &ftl->audio.media_component;

	_nack_destroy(audio_comp);

	return status;
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

		//TODO:  figure out if i need to compensate for rounding error;
		mc->prev_dts_usec = dts_usec;
	}
}

int media_speed_test(ftl_stream_configuration_private_t *ftl, int speed_kbps, int duration_ms) {
	ftl_media_component_common_t *mc = &ftl->audio.media_component;
	int64_t bytes_sent = 0;
	int64_t transmit_level = MAX_MTU;
	unsigned char data[MAX_MTU];
	int64_t bytes_per_ms;
	int64_t total_ms = 0;
	struct timeval stop_tv, start_tv, delta_tv;
	float packet_loss = 0.f;
	int64_t ms_elapsed;
	int64_t total_sent = 0;
	int64_t pkts_sent = 0;

	media_enable_nack(ftl, mc->ssrc, FALSE);

	int prev_sendbuf = 0;
	ftl_get_socket_send_buf(ftl->media.media_socket, &prev_sendbuf);
	FTL_LOG(ftl, FTL_LOG_INFO, "Current sendbuf is %d, setting send buf to %d\n", prev_sendbuf, (speed_kbps * 1000 / 8) / (1000 / 10));
	ftl_set_socket_send_buf(ftl->media.media_socket, (speed_kbps * 1000 / 8) / (1000 / 10));

	int initial_nack_cnt = mc->stats.nack_requests;

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

	/*give some times for the nack requests to come in*/
	sleep_ms(100);

	FTL_LOG(ftl, FTL_LOG_ERROR, "Sent %d bytes in %d ms (%3.2f kbps) lost %d packets\n", total_sent, total_ms, (float)total_sent * 8.f * 1000.f / (float)total_ms, mc->stats.nack_requests - initial_nack_cnt);

	media_enable_nack(ftl, mc->ssrc, TRUE);

	ftl_set_socket_send_buf(ftl->media.media_socket, prev_sendbuf);

	return (int)((float)(mc->stats.nack_requests-initial_nack_cnt) * 100.f / (float)pkts_sent);
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

#ifdef _WIN32
		ReleaseSemaphore(mc->pkt_ready, 1, NULL);
#else
		sem_post(&mc->pkt_ready);
#endif
		mc->stats.packets_queued++;
		mc->stats.bytes_queued += pkt_len;
	}

	mc->stats.current_frame_size += len;

	if (end_of_frame) {
		mc->stats.frames_received++;

		if (mc->stats.current_frame_size > mc->stats.max_frame_size) {
			mc->stats.max_frame_size = mc->stats.current_frame_size;
		}

		if (nalu_type == H264_NALU_TYPE_IDR) {
			FTL_LOG(ftl, FTL_LOG_INFO, "Sent IDR Frame of %d bytes: sn %d-%d\n", mc->stats.current_frame_size, mc->tmp_seq_num, mc->seq_num - 1);
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
		FTL_LOG(ftl, FTL_LOG_ERROR, "sendto() failed with error: %s", ftl_get_socket_error());
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

static int _media_make_video_rtp_packet(ftl_stream_configuration_private_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt) {
	uint8_t sbit, ebit;
	int frag_len;
	ftl_video_component_t *video = &ftl->video;
	ftl_media_component_common_t *mc = &video->media_component;

	sbit = first_pkt ? 1 : 0;
	ebit = (in_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN) <= ftl->media.max_mtu;

	uint32_t rtp_header;
	uint32_t *out_header = (uint32_t *)out;

	rtp_header = htonl((2 << 30) | (mc->payload_type << 16) | mc->seq_num);

	*out_header++ = rtp_header;
	rtp_header = htonl((uint32_t)mc->timestamp);
	*out_header++ = rtp_header;
	rtp_header = htonl(mc->ssrc);
	*out_header++ = rtp_header;

	out = (uint8_t *)out_header;

	mc->seq_num++;

	if (sbit && ebit) {
		sbit = ebit = 0;
		frag_len = in_len;
		*out_len = frag_len + RTP_HEADER_BASE_LEN;
		memcpy(out, in, frag_len);
	}
	else {

		if (sbit) {
			video->fua_nalu_type = in[0];
			in += 1;
			in_len--;
		}

		out[0] = (video->fua_nalu_type & 0x60) | 28;
		out[1] = (sbit << 7) | (ebit << 6) | (video->fua_nalu_type & 0x1F);

		out += 2;

		frag_len = ftl->media.max_mtu - RTP_HEADER_BASE_LEN - RTP_FUA_HEADER_LEN;

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

	uint32_t rtp_header;
	uint32_t *out_header = (uint32_t *)out;

	ftl_audio_component_t *audio = &ftl->audio;
	ftl_media_component_common_t *mc = &audio->media_component;

	rtp_header = htonl((2 << 30) | (1 << 23) | (mc->payload_type << 16) | mc->seq_num);
	*out_header++ = rtp_header;
	rtp_header = htonl((uint32_t)mc->timestamp);
	*out_header++ = rtp_header;
	rtp_header = htonl(mc->ssrc);
	*out_header++ = rtp_header;

	out = (uint8_t *)out_header;

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

#ifdef _WIN32
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
		FTL_LOG(ftl, FTL_LOG_WARN, "Failed to set recv_thread priority to THREAD_PRIORITY_TIME_CRITICAL\n");
	}
#endif

	if ((buf = (unsigned char*)malloc(MAX_PACKET_BUFFER)) == NULL) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to allocate recv buffer\n");
		return (OS_THREAD_TYPE)-1;
	}

#if 0
	if (ret >= 0 && recv_size > 0) {
		if (!discard_recv_data(stream, (size_t)recv_size))
			return -1;
	}
#endif

	while (media->recv_thread_running) {

		ret = recv(media->media_socket, buf, MAX_PACKET_BUFFER, 0);
		if (ret <= 0) {
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
	int disable_flow_control = 0;

	int transmit_level;
	struct timeval start_tv, stop_tv, delta_tv;

#ifdef _WIN32
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
		FTL_LOG(ftl, FTL_LOG_WARN, "Failed to set recv_thread priority to THREAD_PRIORITY_TIME_CRITICAL\n");
	}
#endif

	while (1) {

		if (video_kbps != video->kbps) {
			bytes_per_ms = video->kbps * 1000 / 8 / 1000;
			transmit_level = 5 * bytes_per_ms; /*small initial level to prevent bursting at the start of a stream*/
			video_kbps = video->kbps;

			disable_flow_control = 0;
			if (video_kbps <= 0) {
				disable_flow_control = 1;
			}
		}

#ifdef _WIN32
		WaitForSingleObject(video->pkt_ready, INFINITE);
#else
		sem_wait(&video->pkt_ready);
#endif

		if (!media->send_thread_running) {
			break;
		}

		if (disable_flow_control) {
			_media_send_packet(ftl, video);
		}
		else {
			pkt_sent = 0;
			while (!pkt_sent && media->send_thread_running) {

				if (transmit_level <= 0) {
					sleep_ms(MAX_MTU / bytes_per_ms + 1);
				}

				gettimeofday(&stop_tv, NULL);
				if (!first_packet) {
					timeval_subtract(&delta_tv, &stop_tv, &start_tv);
					transmit_level += (int)timeval_to_ms(&delta_tv) * bytes_per_ms;

					if (transmit_level > (MAX_XMIT_LEVEL_IN_MS * bytes_per_ms)) {
						transmit_level = MAX_XMIT_LEVEL_IN_MS * bytes_per_ms;
					}
				}
				else {
					first_packet = 0;
				}

				start_tv = stop_tv;

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

static int _update_stats(ftl_stream_configuration_private_t *ftl) {
	struct timeval now, delta;
	gettimeofday(&now, NULL);
	timeval_subtract(&delta, &now, &ftl->media.stats_tv);
	float stats_interval = timeval_to_ms(&delta);

	if (stats_interval > 5000) {
		ftl_status_msg_t status;

		ftl->media.stats_tv = now;

		_send_pkt_stats(ftl, &ftl->video.media_component, stats_interval);
		//_send_pkt_stats(ftl, &ftl->audio.media_component);
		_send_video_stats(ftl, &ftl->video.media_component, stats_interval);

		_clear_stats(&ftl->video.media_component.stats);
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

	enqueue_status_msg(ftl, &m);

	return 0;
}

static int _send_video_stats(ftl_stream_configuration_private_t *ftl, ftl_media_component_common_t *mc, float interval_ms) {
	ftl_status_msg_t m;
	ftl_video_frame_stats_msg_t *v = &m.msg.video_stats;
	m.type = FTL_STATUS_VIDEO;

	v->period = (int)interval_ms;
	v->frames_queued = mc->stats.frames_received;
	v->frames_sent = mc->stats.frames_sent;
	v->bytes_queued = mc->stats.bytes_queued;
	v->bytes_sent = mc->stats.bytes_sent;
	v->queue_fullness = (int)(_media_get_queue_fullness(ftl, mc->ssrc) * 100.f);
	v->max_frame_size = mc->stats.max_frame_size;

	enqueue_status_msg(ftl, &m);

	return 0;
}