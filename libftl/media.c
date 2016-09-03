#define __FTL_INTERNAL
#include "ftl_private.h"

int _nack_init(ftl_stream_video_component_private_common_t *ftl, enum obs_encoder_type type) {

	struct media_component *media = &ftl->media[type];

	for (int i = 0; i < NACK_RB_SIZE; i++) {
		if ((media->nack_slots[i] = (nack_slot_t *)malloc(sizeof(nack_slot_t))) == NULL) {
			warn("Failed to allocate memory for nack buffer\n");
			return -1;
		}

		nack_slot_t *slot = media->nack_slots[i];

		if (pthread_mutex_init(&slot->mutex, NULL) != 0) {
			warn("Failed to allocate memory for nack buffer\n");
			return -1;
		}

		slot->len = 0;
		slot->sn = -1;
		slot->insert_ns = 0;
	}

	return 0;
}

int _nack_destroy(ftl_t *ftl, enum obs_encoder_type type) {

	struct media_component *media = &ftl->media[type];

	for (int i = 0; i < NACK_RB_SIZE; i++) {
		if (media->nack_slots[i] != NULL) {
			pthread_mutex_destroy(&media->nack_slots[i]->mutex);
			free(media->nack_slots[i]);
			media->nack_slots[i] = NULL;
		}
	}

	return 0;
}

struct media_component *_media_lookup(ftl_t *ftl, uint32_t ssrc) {
	struct media_component *media = NULL;

	for (int i = 0; i < sizeof(ftl->media) / sizeof(ftl->media[0]); i++) {
		if (ftl->media[i].ssrc == ssrc) {
			media = &ftl->media[i];
			break;
		}
	}

	return media;
}

uint8_t* _nack_get_empty_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int *buf_len) {
	struct media_component *media;

	if ((media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return NULL;
	}

	/*map sequence number to slot*/
	nack_slot_t *slot = media->nack_slots[sn % NACK_RB_SIZE];

	pthread_mutex_lock(&slot->mutex);

	*buf_len = sizeof(slot->packet);
	return slot->packet;
}

int _nack_send_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int len) {
	struct media_component *media;
	int tx_len;

	if ((media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	/*map sequence number to slot*/
	nack_slot_t *slot = media->nack_slots[sn % NACK_RB_SIZE];

	slot->len = len;
	slot->sn = sn;
	slot->insert_ns = os_gettime_ns();

	if ((tx_len = sendto(ftl->data_sock, slot->packet, slot->len, 0, (struct sockaddr*) &ftl->server_addr, sizeof(ftl->server_addr))) == SOCKET_ERROR)
	{
		warn("sendto() failed with error code : %d", WSAGetLastError());
	}

	pthread_mutex_unlock(&slot->mutex);

	return tx_len;
}

int nack_resend_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn) {
	struct media_component *media;
	int tx_len;

	if ((media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	/*map sequence number to slot*/
	nack_slot_t *slot = media->nack_slots[sn % NACK_RB_SIZE];

	pthread_mutex_lock(&slot->mutex);

	if (slot->sn != sn) {
		warn("[%d] expected sn %d in slot but found %d...discarding retransmit request\n", ssrc, sn, slot->sn);
		pthread_mutex_unlock(&slot->mutex);
		return 0;
	}

	uint64_t req_delay = os_gettime_ns() - slot->insert_ns;

	tx_len = _nack_send_packet(ftl, ssrc, sn, slot->len);
	info("[%d] resent sn %d, request delay was %d ms\n", ssrc, sn, req_delay / 1000000);

	return tx_len;
}

int _make_video_rtp_packet(ftl_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt) {
	uint8_t sbit, ebit;
	int frag_len;
	struct media_component *media = &ftl->media[OBS_ENCODER_VIDEO];

	sbit = first_pkt ? 1 : 0;
	ebit = (in_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN) < ftl->max_mtu;

	uint32_t rtp_header;

	rtp_header = htonl((2 << 30) | (media->payload_type << 16) | media->seq_num);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->timestamp);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->ssrc);
	*((uint32_t*)out)++ = rtp_header;

	media->seq_num++;

	if (sbit && ebit) {
		sbit = ebit = 0;
		frag_len = in_len;
		*out_len = frag_len + RTP_HEADER_BASE_LEN;
		memcpy(out, in, frag_len);
	}
	else {

		if (sbit) {
			media->fua_nalu_type = in[0];
			in += 1;
			in_len--;
		}

		out[0] = media->fua_nalu_type & 0xE0 | 28;
		out[1] = (sbit << 7) | (ebit << 6) | (media->fua_nalu_type & 0x1F);

		out += 2;

		frag_len = ftl->max_mtu - RTP_HEADER_BASE_LEN - RTP_FUA_HEADER_LEN;

		if (frag_len > in_len) {
			frag_len = in_len;
		}

		memcpy(out, in, frag_len);

		*out_len = frag_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN;
	}

	return frag_len + sbit;
}

int _make_audio_rtp_packet(ftl_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len) {
	int payload_len = in_len;

	uint32_t rtp_header;

	struct media_component *media = &ftl->media[OBS_ENCODER_AUDIO];

	rtp_header = htonl((2 << 30) | (1 << 23) | (media->payload_type << 16) | media->seq_num);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->timestamp);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->ssrc);
	*((uint32_t*)out)++ = rtp_header;

	media->seq_num++;
	media->timestamp += media->timestamp_step;

	memcpy(out, in, payload_len);

	*out_len = payload_len + RTP_HEADER_BASE_LEN;

	return in_len;
}

int _set_marker_bit(ftl_t *ftl, enum obs_encoder_type type, uint8_t *in) {
	uint32_t rtp_header;

	rtp_header = ntohl(*((uint32_t*)in));
	rtp_header |= 1 << 23; /*set marker bit*/
	*((uint32_t*)in) = htonl(rtp_header);

	ftl->media[type].timestamp += ftl->media[type].timestamp_step;

	return 0;
}

static void *recv_thread(void *data)
{
	ftl_t *ftl = (ftl_t *)data;
	int ret;
	unsigned char *buf;

	if ((buf = (unsigned char*)malloc(MAX_PACKET_MTU)) == NULL) {
		printf("Failed to allocate recv buffer\n");
		return NULL;
	}

#if 0
#ifdef _WIN32
	ret = ioctlsocket(stream->sb_socket, FIONREAD,
		(u_long*)&recv_size);
#else
	ret = ioctl(stream->sb_socket, FIONREAD, &recv_size);
#endif

	if (ret >= 0 && recv_size > 0) {
		if (!discard_recv_data(stream, (size_t)recv_size))
			return -1;
	}
#endif
	//os_set_thread_name("ftl-stream: recv_thread");

	while (ftl->recv_thread_running) {

#ifdef _WIN32
		ret = recv(ftl->data_sock, buf, MAX_PACKET_MTU, 0);
#else
		ret = recv(stream->sb_socket, buf, MAX_PACKET_MTU, 0);
#endif
		if (ret <= 0) {
			continue;
		}

		int version, padding, feedbackType, ptype, length, ssrcSender, ssrcMedia;
		uint16_t snBase, blp, sn;
		int recv_len = ret;

		if (recv_len < 2) {
			warn("recv packet too small to parse, discarding\n");
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
				warn("reported len was %d but packet is only %d...discarding\n", recv_len, ((length + 1) * 4));
				continue;
			}

			ssrcSender = ntohl(*((uint32_t*)(buf + 4)));
			ssrcMedia = ntohl(*((uint32_t*)(buf + 8)));

			uint16_t *p = (uint16_t *)(buf + 12);

			for (int fci = 0; fci < (length - 2); fci++) {
				//request the first sequence number
				snBase = ntohs(*p++);
				nack_resend_packet(ftl, ssrcMedia, snBase);
				blp = ntohs(*p++);
				if (blp) {
					for (int i = 0; i < 16; i++) {
						if ((blp & (1 << i)) != 0) {
							sn = snBase + i + 1;
							nack_resend_packet(ftl, ssrcMedia, sn);
						}
					}
				}
			}
		}
	}

	info("Exited Recv Thread\n");

	return 0;
}