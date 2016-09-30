
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
  ftl_cfg->ready_for_media = 0;
  ftl_cfg->video_kbps = params->video_kbps;

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

  ftl_cfg->audio.codec = params->audio_codec;
  ftl_cfg->video.codec = params->video_codec;

  ftl_cfg->audio.media_component.payload_type = AUDIO_PTYPE;
  ftl_cfg->video.media_component.payload_type = VIDEO_PTYPE;

  //TODO: this should be randomly generated, there is a potential for ssrc collisions with this
  ftl_cfg->audio.media_component.ssrc = ftl_cfg->channel_id;
  ftl_cfg->video.media_component.ssrc = ftl_cfg->channel_id + 1;

  ftl_cfg->video.frame_rate = params->video_frame_rate;
  ftl_cfg->video.width = 1280;
  ftl_cfg->video.height = 720;


  ftl_register_log_handler(params->log_func);

  ftl_cfg->status_q.count = 0;
  ftl_cfg->status_q.head = NULL;

#ifdef _WIN32
  if ((ftl_cfg->status_q.mutex = CreateMutex(NULL, FALSE, NULL)) == NULL) {
#else
  if (pthread_mutex_init(&ftl_cfg->status_q.mutex, NULL) != 0) {
#endif
	  FTL_LOG(FTL_LOG_ERROR, "Failed to create status queue mutex\n");
	  return FTL_MALLOC_FAILURE;
  }

#ifdef _WIN32
  if ((ftl_cfg->status_q.sem = CreateSemaphore(NULL, 0, MAX_STATUS_MESSAGE_QUEUED, NULL)) == NULL) {
#else
  ftl_cfg->status_q.sem
#endif
	  FTL_LOG(FTL_LOG_ERROR, "Failed to allocate create status queue semaphore\n");
	  return FTL_MALLOC_FAILURE;
  }

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

  if ((status = media_init(ftl_cfg)) != FTL_SUCCESS) {
	  return status;
  }

  ftl_cfg->ready_for_media = 1;

  return status;
}

FTL_API ftl_status_t ftl_ingest_get_status(ftl_handle_t *ftl_handle, ftl_status_msg_t *msg, int ms_timeout) {
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_status_t status = FTL_SUCCESS;

	return dequeue_status_msg(ftl, msg, ms_timeout);
}

FTL_API ftl_status_t ftl_ingest_update_hostname(ftl_handle_t *ftl_handle, const char *ingest_hostname) {

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_update_stream_key(ftl_handle_t *ftl_handle, const char *stream_key) {
	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_send_media(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, uint8_t *data, int32_t len, int end_of_frame) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->private;

	if (!ftl->ready_for_media) {
		return FTL_NOT_CONNECTED;
	}

	if (media_type == FTL_AUDIO_DATA) {
		media_send_audio(ftl, data, len);
	}
	else if (media_type == FTL_VIDEO_DATA) {
		media_send_video(ftl, data, len, end_of_frame);
	}
	else {
		return FTL_UNSUPPORTED_MEDIA_TYPE;
	}

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_disconnect(ftl_handle_t *ftl_handle) {
	ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_status_t status_code;

	ftl_cfg->ready_for_media = 0;

	if ((status_code = _ingest_disconnect(ftl_cfg)) != FTL_SUCCESS) {
		FTL_LOG(FTL_LOG_ERROR, "Disconnect failed with error %d\n", status_code);
	}

	if ((status_code = media_destroy(ftl_cfg)) != FTL_SUCCESS) {
		FTL_LOG(FTL_LOG_ERROR, "failed to clean up media channel with error %d\n", status_code);
	}

	ftl_status_msg_t status;

	status.type = FTL_STATUS_EVENT;
	status.msg.event.reason = FTL_STATUS_EVENT_REASON_API_REQUEST;
	status.msg.event.type = FTL_STATUS_EVENT_TYPE_DISCONNECTED;

	enqueue_status_msg(ftl_cfg, &status);

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_destroy(ftl_handle_t *ftl_handle){
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->private;
	ftl_status_t status = FTL_SUCCESS;

	if (ftl != NULL) {

#ifdef _WIN32
		WaitForSingleObject(ftl->status_q.mutex, INFINITE);
#else
		pthread_mutex_lock(&ftl->status_q.mutex);
#endif

		status_queue_elmt_t *elmt;

		while (ftl->status_q.head != NULL) {
			elmt = ftl->status_q.head;
			ftl->status_q.head = elmt->next;
			free(elmt);
			ftl->status_q.count--;
		}

		ftl->status_q.head = NULL;

#ifdef _WIN32
		ReleaseMutex(ftl->status_q.mutex);
		CloseHandle(ftl->status_q.mutex);
		CloseHandle(ftl->status_q.sem);
#else
		pthread_mutex_unlock(&ftl->status_q.mutex);
		pthread_mutex_destroy(&ftl->status_q.mutex);
#endif

		if (ftl->key != NULL) {
			free(ftl->key);
		}

		free(ftl);
	}

	return status;
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