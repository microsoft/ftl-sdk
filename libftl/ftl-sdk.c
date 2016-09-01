
#include "ftl.h"
#include "ftl_private.h"

static bool _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key);
static int _lookup_ingest_ip(const char *ingest_location, char *ingest_ip);

FTL_API ftl_status_t ftl_init(){
  return FTL_SUCCESS;
}

FTL_API ftl_status_t ftl_ingest_create(ftl_handle_t *ftl_handle, ftl_ingest_params_t *params){
  ftl_status_t ret_status = FTL_SUCCESS;
	ftl_stream_configuration_private_t ftl_cfg;

  if( (ftl_cfg = (ftl_stream_configuration_private_t *)malloc(sizeof(ftl_stream_configuration_private_t)) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  ftl_cfg->connected = 0;

  ftl_cfg->key = NULL;
  if( (ftl_cfg->key = (char*)malloc(sizeof(char)*MAX_KEY_LEN)) == NULL){
    ret_status = FTL_MALLOC_FAILURE;
		goto fail;
  }

  if ( _get_chan_id_and_key(params->stream_key, &ftl_cfg->channel_id, ftl_cfg->key) == false ) {
    ret_status = FTL_BAD_OR_INVALID_STREAM_KEY;
		goto fail;
  }

/*because some of our ingests are behind revolving dns' we need to store the ip to ensure it doesnt change for handshake and media*/
  if ( _lookup_ingest_ip(params->ingest_hostname, ftl_cfg->ingest_ip) == false ) {
    ret_status = FTL_DNS_FAILURE;
		goto fail;
  }

  ftl_cfg->audio.payload_type = AUDIO_PTYPE;
  ftl_cfg->video.payload_type = VIDEO_PTYPE;

  //TODO: this should be randomly generated, there is a potential for ssrc collisions with this
  ftl_cfg->audio.ssrc = ftl_cfg->channel_id;
  ftl_cfg->video.ssrc = ftl_cfg->channel_id + 1;

	ftl_handle->private = ftl_cfg;
  return ret_status;

fail:

	if(*ftl_handle != NULL) {
		if (ftl_cfg->key != NULL) {
			free(ftl_cfg->key);
		}

		free(ftl_cfg);
	}

	return ret_status;	
}

FTL_API ftl_status_t ftl_ingest_connect(ftl_handle_t *ftl_handle){
  ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;


}

FTL_API ftl_status_t ftl_ingest_get_status(ftl_handle_t *ftl_handle);

FTL_API ftl_status_t ftl_ingest_update_hostname(ftl_handle_t *ftl_handle, const char *ingest_hostname);
FTL_API ftl_status_t ftl_ingest_update_stream_key(ftl_handle_t *ftl_handle, const char *stream_key);

FTL_API ftl_status_t ftl_ingest_send_media(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, uint8_t *data, int32 len);

FTL_API ftl_status_t ftl_ingest_disconnect(ftl_handle_t *ftl_handle);

FTL_API ftl_status_t ftl_ingest_destroy(ftl_handle_t *ftl_handle);

bool _get_chan_id_and_key(const char *stream_key, uint32_t *chan_id, char *key) {
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

			return true;
		}
	}

		return false;
}


static int _lookup_ingest_ip(const char *ingest_location, char *ingest_ip) {
	struct hostent *remoteHost;
	struct in_addr addr;
	bool success = false;
	ingest_ip[0] = '\0';

	remoteHost = gethostbyname(ingest_location);

	if (remoteHost) {
		int i = 0;
		if (remoteHost->h_addrtype == AF_INET)
		{
			while (remoteHost->h_addr_list[i] != 0) {
				addr.s_addr = *(u_long *)remoteHost->h_addr_list[i++];
				blog(LOG_INFO, "IP Address #%d of ingest is: %s\n", i, inet_ntoa(addr));

				//revolving dns ensures this will change automatically so just use first ip found
				if (!success) {
					strcpy(ingest_ip, inet_ntoa(addr));
					success = true;
          //break;
				}
			}
		}
	}

	return success;
}