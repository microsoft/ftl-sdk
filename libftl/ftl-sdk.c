
#define __FTL_INTERNAL
#include "ftl.h"
#include "ftl_private.h"

static BOOL _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key);
static int _lookup_ingest_ip(const char *ingest_location, char *ingest_ip);

char error_message[1000];
FTL_API const int FTL_VERSION_MAJOR = 0;
FTL_API const int FTL_VERSION_MINOR = 2;
FTL_API const int FTL_VERSION_MAINTENANCE = 3;

// Initializes all sublibraries used by FTL
FTL_API ftl_status_t ftl_init() {
  ftl_init_sockets();
  ftl_logging_init();
  return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_create(ftl_handle_t *ftl_handle, ftl_ingest_params_t *params){
  ftl_status_t ret_status = FTL_SUCCESS;
	ftl_stream_configuration_private_t *ftl_cfg = NULL;

  if( (ftl_cfg = (ftl_stream_configuration_private_t *)malloc(sizeof(ftl_stream_configuration_private_t))) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  ftl_cfg->connected = 0;

  ftl_cfg->key = NULL;
  if( (ftl_cfg->key = (char*)malloc(sizeof(char)*MAX_KEY_LEN)) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  if ( _get_chan_id_and_key(params->stream_key, &ftl_cfg->channel_id, ftl_cfg->key) == FALSE ) {
    ret_status = FTL_BAD_OR_INVALID_STREAM_KEY;
		goto fail;
  }

/*because some of our ingests are behind revolving dns' we need to store the ip to ensure it doesnt change for handshake and media*/
  if ( _lookup_ingest_ip(params->ingest_hostname, ftl_cfg->ingest_ip) == FALSE) {
    ret_status = FTL_DNS_FAILURE;
		goto fail;
  }

  ftl_cfg->audio.media_component.payload_type = AUDIO_PTYPE;
  ftl_cfg->video.media_component.payload_type = VIDEO_PTYPE;

  //TODO: this should be randomly generated, there is a potential for ssrc collisions with this
  ftl_cfg->audio.media_component.ssrc = ftl_cfg->channel_id;
  ftl_cfg->video.media_component.ssrc = ftl_cfg->channel_id + 1;

  ftl_cfg->video.frame_rate = params->video_frame_rate;

  ftl_handle->private = ftl_cfg;
  return ret_status;

fail:

	if(ftl_cfg != NULL) {
		if (ftl_cfg->key != NULL) {
			free(ftl_cfg->key);
		}

		free(ftl_cfg);
	}

	return ret_status;	
}

FTL_API ftl_status_t ftl_ingest_connect(ftl_handle_t *ftl_handle){
  ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;
  ftl_status_t status = FTL_SUCCESS;

  if ((status = _ingest_connect(ftl_cfg)) != FTL_SUCCESS) {
	  return status;
  }

  if ((status = _media_init(ftl_cfg)) != FTL_SUCCESS) {
	  return status;
  }

  return status;
}

FTL_API ftl_status_t ftl_ingest_get_status(ftl_handle_t *ftl_handle) {

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_update_hostname(ftl_handle_t *ftl_handle, const char *ingest_hostname) {

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_update_stream_key(ftl_handle_t *ftl_handle, const char *stream_key) {
	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_send_media(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, uint8_t *data, int32_t len) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_media_component_common_t *mc = NULL;

	if (media_type == FTL_AUDIO_DATA) {
		mc = &ftl->audio.media_component;
	}
	else if (media_type == FTL_VIDEO_DATA) {
		mc = &ftl->video.media_component;
	}
	else {
		return FTL_UNSUPPORTED_MEDIA_TYPE;
	}

	int pkt_len;
	int payload_size;
	int consumed = 0;

	int remaining = len;
	int first_fu = 1;

	while (remaining > 0) {
		uint16_t sn = mc->seq_num;
		uint32_t ssrc = mc->ssrc;
		uint8_t *pkt_buf;
		pkt_buf = media_get_empty_packet(ftl, ssrc, sn, &pkt_len);

		payload_size = media_make_video_rtp_packet(ftl, pkt_buf, remaining, pkt_buf, &pkt_len, first_fu);

		first_fu = 0;
		remaining -= payload_size;
		consumed += payload_size;
		pkt_buf += payload_size;

		/*if all data has been consumed set marker bit*/
		if (remaining <= 0) {
			media_set_marker_bit(ftl, pkt_buf);
		}

		media_send_packet(ftl, ssrc, sn, pkt_len);
	}

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_disconnect(ftl_handle_t *ftl_handle) {
	ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_status_t status;

	status = _ingest_disconnect(ftl_cfg);

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_destroy(ftl_handle_t *ftl_handle){
	ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_status_t status;

	if (ftl_cfg != NULL) {
		if (ftl_cfg->key != NULL) {
			free(ftl_cfg->key);
		}

		free(ftl_cfg);
	}

	return FTL_SUCCESS;
}

BOOL _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key) {
	int len;
	
	len = strlen(stream_key);
	for (int i = 0; i != len; i++) {
		/* find the comma that divides the stream key */
		if (stream_key[i] == '-' || stream_key[i] == ',') {
			/* stream key gets copied */
			strcpy(key, stream_key+i+1);

			/* Now get the channel id */
			char * copy_of_key = strdup(stream_key);
			copy_of_key[i] = '\0';
			*chan_id = atol(copy_of_key);
			free(copy_of_key);

			return TRUE;
		}
	}

		return FALSE;
}


static int _lookup_ingest_ip(const char *ingest_location, char *ingest_ip) {
	struct hostent *remoteHost;
	struct in_addr addr;
	BOOL success = FALSE;
	ingest_ip[0] = '\0';

	remoteHost = gethostbyname(ingest_location);

	if (remoteHost) {
		int i = 0;
		if (remoteHost->h_addrtype == AF_INET)
		{
			while (remoteHost->h_addr_list[i] != 0) {
				addr.s_addr = *(u_long *)remoteHost->h_addr_list[i++];
				FTL_LOG(FTL_LOG_DEBUG, "IP Address #%d of ingest is: %s\n", i, inet_ntoa(addr));

				//revolving dns ensures this will change automatically so just use first ip found
				if (!success) {
					strcpy(ingest_ip, inet_ntoa(addr));
					success = TRUE;
          //break;
				}
			}
		}
	}

	return success;
}