
#define __FTL_INTERNAL
#include "ftl.h"
#include "ftl_private.h"
#include <curl/curl.h>

static BOOL _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key);
static int _lookup_ingest_ip(const char *ingest_location, char *ingest_ip);

char error_message[1000];
FTL_API const int FTL_VERSION_MAJOR = 0;
FTL_API const int FTL_VERSION_MINOR = 8;
FTL_API const int FTL_VERSION_MAINTENANCE = 4;

// Initializes all sublibraries used by FTL
FTL_API ftl_status_t ftl_init() {
  init_sockets();
  os_init();
  curl_global_init(CURL_GLOBAL_ALL);
  return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_create(ftl_handle_t *ftl_handle, ftl_ingest_params_t *params){
  ftl_status_t ret_status = FTL_SUCCESS;
	ftl_stream_configuration_private_t *ftl = NULL;

  if( (ftl = (ftl_stream_configuration_private_t *)malloc(sizeof(ftl_stream_configuration_private_t))) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  ftl->connected = 0;
  ftl->ingest_socket = 0;
  ftl->async_queue_alive = 0;
  ftl->ready_for_media = 0;
  ftl->ingest_list = NULL;
  ftl->video.media_component.peak_kbps = params->peak_kbps;

  ftl->key = NULL;
  if( (ftl->key = (char*)malloc(sizeof(char)*MAX_KEY_LEN)) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  if ( _get_chan_id_and_key(params->stream_key, &ftl->channel_id, ftl->key) == FALSE ) {
    ret_status = FTL_BAD_OR_INVALID_STREAM_KEY;
		goto fail;
  }

  ftl->audio.codec = params->audio_codec;
  ftl->video.codec = params->video_codec;

  ftl->audio.media_component.payload_type = AUDIO_PTYPE;
  ftl->video.media_component.payload_type = VIDEO_PTYPE;

  //TODO: this should be randomly generated, there is a potential for ssrc collisions with this
  ftl->audio.media_component.ssrc = ftl->channel_id;
  ftl->video.media_component.ssrc = ftl->channel_id + 1;

  ftl->video.fps_num = params->fps_num;
  ftl->video.fps_den = params->fps_den;
  ftl->video.dts_usec = 0;
  ftl->audio.dts_usec = 0;
  ftl->video.dts_error = 0;

  strncpy_s(ftl->vendor_name, sizeof(ftl->vendor_name) / sizeof(ftl->vendor_name[0]), params->vendor_name, sizeof(ftl->vendor_name) / sizeof(ftl->vendor_name[0]) - 1);
  strncpy_s(ftl->vendor_version, sizeof(ftl->vendor_version) / sizeof(ftl->vendor_version[0]), params->vendor_version, sizeof(ftl->vendor_version) / sizeof(ftl->vendor_version[0]) - 1);

  /*this is legacy, this isnt used anymore*/
  ftl->video.width = 1280;
  ftl->video.height = 720;

  ftl->status_q.count = 0;
  ftl->status_q.head = NULL;

  os_init_mutex(&ftl->status_q.mutex);

  if (os_semaphore_create(&ftl->status_q.sem, "/StatusQueue", O_CREAT, 0) < 0) {
	  return FTL_MALLOC_FAILURE;
  }

  ftl->async_queue_alive = 1;
  
  char *ingest_ip = NULL;

  if (strcmp(params->ingest_hostname, "auto") == 0) {
	  ingest_ip = ingest_find_best(ftl);
  }
  else {
	  ingest_ip = ingest_get_ip(ftl, params->ingest_hostname);
  }

  if (ingest_ip == NULL) {
	  ret_status = FTL_DNS_FAILURE;
	  goto fail;
  }

  strcpy_s(ftl->ingest_ip, sizeof(ftl->ingest_ip), ingest_ip);
 
  ftl_handle->priv = ftl;
  return ret_status;

fail:

	if(ftl != NULL) {
		if (ftl->key != NULL) {
			free(ftl->key);
		}

		free(ftl);
	}

	return ret_status;	
}

FTL_API ftl_status_t ftl_ingest_connect(ftl_handle_t *ftl_handle){
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
  ftl_status_t status = FTL_SUCCESS;

  if ((status = _init_control_connection(ftl)) != FTL_SUCCESS) {
	  return status;
  }

  if ((status = _ingest_connect(ftl)) != FTL_SUCCESS) {
	  return status;
  }

  if ((status = media_init(ftl)) != FTL_SUCCESS) {
	  return status;
  }
  
  return status;
}

FTL_API ftl_status_t ftl_ingest_get_status(ftl_handle_t *ftl_handle, ftl_status_msg_t *msg, int ms_timeout) {
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	ftl_status_t status = FTL_SUCCESS;

	if (ftl == NULL) {
		return FTL_NOT_INITIALIZED;
	}

	return dequeue_status_msg(ftl, msg, ms_timeout);
}

FTL_API ftl_status_t ftl_ingest_update_params(ftl_handle_t *ftl_handle, ftl_ingest_params_t *params) {
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	ftl_status_t status = FTL_SUCCESS;

	ftl->video.media_component.peak_kbps = params->peak_kbps;

	/* not going to update fps for the moment*/
	/*
	ftl->video.fps_num = params->fps_num;
	ftl->video.fps_den = params->fps_den;
	*/

	return status;
}

FTL_API int ftl_ingest_speed_test(ftl_handle_t *ftl_handle, int speed_kbps, int duration_ms) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;

	int peak_bw = media_speed_test(ftl, speed_kbps, duration_ms);

	return peak_bw;
}

FTL_API int ftl_ingest_send_media_dts(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, int64_t dts_usec, uint8_t *data, int32_t len, int end_of_frame) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	int bytes_sent = 0;

	if (media_type == FTL_AUDIO_DATA) {
		bytes_sent = media_send_audio(ftl, dts_usec, data, len);
	}
	else if (media_type == FTL_VIDEO_DATA) {
		bytes_sent = media_send_video(ftl, dts_usec, data, len, end_of_frame);
	}
	else {
		return bytes_sent;
	}

	return bytes_sent;
}

FTL_API int ftl_ingest_send_media(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, uint8_t *data, int32_t len, int end_of_frame) {

	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	int64_t dts_increment_usec, dts_usec;

	if (media_type == FTL_AUDIO_DATA) {
		dts_usec = ftl->audio.dts_usec;
		dts_increment_usec = AUDIO_PACKET_DURATION_MS * 1000;
		ftl->audio.dts_usec += dts_increment_usec;
	}
	else if (media_type == FTL_VIDEO_DATA) {
		dts_usec = ftl->video.dts_usec;
		if (end_of_frame) {
			float dst_usec_f = (float)ftl->video.fps_den * 1000000.f / (float)ftl->video.fps_num + ftl->video.dts_error;
			dts_increment_usec = (int64_t)(dst_usec_f);
			ftl->video.dts_error = dst_usec_f - (float)dts_increment_usec;
			ftl->video.dts_usec += dts_increment_usec;
		}
	}

	return ftl_ingest_send_media_dts(ftl_handle, media_type, dts_usec, data, len, end_of_frame);
}

FTL_API ftl_status_t ftl_ingest_disconnect(ftl_handle_t *ftl_handle) {
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	ftl_status_t status_code;

	if (!ftl->connected) {
		return FTL_SUCCESS;
	}

	status_code = _internal_ingest_disconnect(ftl);

	FTL_LOG(ftl, FTL_LOG_ERROR, "Sending kill event\n");
	ftl_status_msg_t status;
	status.type = FTL_STATUS_EVENT;
	status.msg.event.reason = FTL_STATUS_EVENT_REASON_API_REQUEST;
	status.msg.event.type = FTL_STATUS_EVENT_TYPE_DISCONNECTED;
	status.msg.event.error_code = FTL_USER_DISCONNECT;

	enqueue_status_msg(ftl, &status);

	return status_code;
}

ftl_status_t _internal_ingest_disconnect(ftl_stream_configuration_private_t *ftl) {

	ftl_status_t status_code;

	if ((status_code = _ingest_disconnect(ftl)) != FTL_SUCCESS) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "Disconnect failed with error %d\n", status_code);
	}

	if ((status_code = media_destroy(ftl)) != FTL_SUCCESS) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "failed to clean up media channel with error %d\n", status_code);
	}

	return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_destroy(ftl_handle_t *ftl_handle){
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)ftl_handle->priv;
	ftl_status_t status = FTL_SUCCESS;

	if (ftl != NULL) {

		os_lock_mutex(&ftl->status_q.mutex);

		ftl->async_queue_alive = 0;
		os_semaphore_post(&ftl->status_q.sem); //if someone is blocked on the semaphore, unblock it

		status_queue_elmt_t *elmt;

		while (ftl->status_q.head != NULL) {
			elmt = ftl->status_q.head;
			ftl->status_q.head = elmt->next;
			free(elmt);
			ftl->status_q.count--;
		}

		os_unlock_mutex(&ftl->status_q.mutex);
		os_delete_mutex(&ftl->status_q.mutex);

		os_semaphore_delete(&ftl->status_q.sem);

		if (ftl->key != NULL) {
			free(ftl->key);
		}

		free(ftl);
		ftl_handle->priv = NULL;
	}

#ifdef _WIN32
	_CrtDumpMemoryLeaks();
#endif

	return status;
}

char* ftl_status_code_to_string(ftl_status_t status) {

	switch (status) {
	case FTL_SUCCESS:
		return "Success";
	case FTL_SOCKET_NOT_CONNECTED:
		return "The socket is no longer connected";
	case FTL_MALLOC_FAILURE:
		return "Internal memory allocation error";
	case FTL_INTERNAL_ERROR:
		return "An Internal error occurred";
	case FTL_CONFIG_ERROR:
		return "The parameters supplied are invalid or incomplete";
	case FTL_NOT_ACTIVE_STREAM:
		return "The stream is not active";
	case FTL_NOT_CONNECTED:
		return "The channel is not connected";
	case FTL_ALREADY_CONNECTED:
		return "The channel is already connected";
	case FTL_STATUS_TIMEOUT:
		return "Timed out waiting for status message";
	case FTL_QUEUE_FULL:
		return "The status queue is full";
	case FTL_STATUS_WAITING_FOR_KEY_FRAME:
		return "dropping packets until a key frame is recevied";
	case FTL_QUEUE_EMPTY:
		return "The status queue is empty";
	case FTL_NOT_INITIALIZED:
		return "The parameters were not correctly initialized";
	case FTL_BAD_REQUEST:
		return "A request to the ingest was invalid";
	case FTL_DNS_FAILURE:
		return "Failed to get an ip address for the specified ingest (DNS lookup failure)";
	case FTL_CONNECT_ERROR:
		return "An unknown error occurred connecting to the socket";
	case FTL_UNSUPPORTED_MEDIA_TYPE:
		return "The specified media type is not supported";
	case FTL_OLD_VERSION:
		return "The current version of the FTL-SDK is no longer supported";
	case FTL_UNAUTHORIZED:
		return "This channel is not authorized to connect to this ingest";
	case FTL_AUDIO_SSRC_COLLISION:
		return "The Audio SSRC is already in use";
	case FTL_VIDEO_SSRC_COLLISION:
		return "The Video SSRC is already in use";
	case FTL_STREAM_REJECTED:
		return "The Ingest rejected the stream";
	case FTL_BAD_OR_INVALID_STREAM_KEY:
		return "Invalid stream key";
	case FTL_CHANNEL_IN_USE:
		return "Channel is already actively streaming";
	case FTL_REGION_UNSUPPORTED:
		return "The location you are attempting to stream from is not authorized to do so by the local government";
	case FTL_NO_MEDIA_TIMEOUT:
		return "The ingest did not receive any audio or video media for an extended period of time";
	case FTL_USER_DISCONNECT:
		return "ftl ingest disconnect api was called";
	case FTL_UNKNOWN_ERROR_CODE:
	default:
		/* Unknown FTL error */
		return "Unknown status code";
	}
}

BOOL _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key) {
	size_t len;
	int i;
	
	len = strlen(stream_key);
	for (i = 0; i != len; i++) {
		/* find the comma that divides the stream key */
		if (stream_key[i] == '-' || stream_key[i] == ',') {
			/* stream key gets copied */
			strcpy_s(key, MAX_KEY_LEN, stream_key+i+1);

			/* Now get the channel id */
			char * copy_of_key = _strdup(stream_key);
			copy_of_key[i] = '\0';
			*chan_id = atol(copy_of_key);
			free(copy_of_key);

			return TRUE;
		}
	}

		return FALSE;
}


