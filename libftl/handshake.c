/**
 * activate.c - Activates an FTL stream
 *
 * Copyright (c) 2015 Michael Casadevall
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 **/

#define __FTL_INTERNAL
#include "ftl.h"
#include "ftl_private.h"
#include <stdarg.h>

#ifdef _WIN32
DWORD WINAPI connection_status_thread(LPVOID data);
#else
static void *connection_status_thread(void *data);
#endif

static ftl_response_code_t _ftl_send_command(ftl_stream_configuration_private_t *ftl_cfg, BOOL need_response, char *response_buf, int response_len, const char *cmd_fmt, ...);
ftl_status_t _log_response(ftl_stream_configuration_private_t *ftl, int response_code);

ftl_status_t _init_control_connection(ftl_stream_configuration_private_t *ftl) {
	ftl_response_code_t response_code = FTL_INGEST_RESP_UNKNOWN;

	int err = 0;
	SOCKET sock = 0;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	struct addrinfo* resolved_names = 0;
	struct addrinfo* p = 0;

	int ingest_port = INGEST_PORT;
	char ingest_port_str[10];

	if (ftl->connected) {
		return FTL_ALREADY_CONNECTED;
	}

	snprintf(ingest_port_str, 10, "%d", ingest_port);

	err = getaddrinfo(ftl->ingest_ip, ingest_port_str, &hints, &resolved_names);
	if (err != 0) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "getaddrinfo failed to look up ingest address %s.", ftl->ingest_ip);
		FTL_LOG(ftl, FTL_LOG_ERROR, "gai error was: %s", gai_strerror(err));
		return FTL_DNS_FAILURE;
	}

	/* Open a socket to the control port */
	for (p = resolved_names; p != NULL; p = p->ai_next) {
		sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sock == -1) {
			/* try the next candidate */
			FTL_LOG(ftl, FTL_LOG_DEBUG, "failed to create socket. error: %s", get_socket_error());
			continue;
		}

		/* Go for broke */
		if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == -1) {
			FTL_LOG(ftl, FTL_LOG_DEBUG, "failed to connect on candidate, error: %s", get_socket_error());
			close_socket(sock);
			sock = 0;
			continue;
		}

		/* If we got here, we successfully connected */
		if (set_socket_enable_keepalive(sock) != 0) {
			FTL_LOG(ftl, FTL_LOG_DEBUG, "failed to enable keep alives.  error: %s", get_socket_error());
		}

		if (set_socket_recv_timeout(sock, SOCKET_RECV_TIMEOUT_MS) != 0) {
			FTL_LOG(ftl, FTL_LOG_DEBUG, "failed to set recv timeout.  error: %s", get_socket_error());
		}

		if (set_socket_send_timeout(sock, SOCKET_SEND_TIMEOUT_MS) != 0) {
			FTL_LOG(ftl, FTL_LOG_DEBUG, "failed to set send timeout.  error: %s", get_socket_error());
		}

		break;
	}

	/* Free the resolved name struct */
	freeaddrinfo(resolved_names);

	/* Check to see if we actually connected */
	if (sock <= 0) {
		FTL_LOG(ftl, FTL_LOG_ERROR, "failed to connect to ingest. Last error was: %s",
			get_socket_error());
		return FTL_CONNECT_ERROR;
	}

	ftl->ingest_socket = sock;

	return FTL_SUCCESS;
}

ftl_status_t _ingest_connect(ftl_stream_configuration_private_t *ftl) {
  ftl_response_code_t response_code = FTL_INGEST_RESP_UNKNOWN;

  int err = 0;
  char response[MAX_INGEST_COMMAND_LEN];

  if (ftl->connected) {
	  return FTL_ALREADY_CONNECTED;
  }

  if (ftl->ingest_socket < 0) {
	  return FTL_SOCKET_NOT_CONNECTED;
  }

  if(!ftl_get_hmac(ftl->ingest_socket, ftl->key, ftl->hmacBuffer)) {
    FTL_LOG(ftl, FTL_LOG_ERROR, "could not get a signed HMAC!");
    response_code = FTL_INTERNAL_ERROR;
    goto fail;    
  }

  if ( (response_code = _ftl_send_command(ftl, TRUE, response, sizeof(response), "CONNECT %d $%s", ftl->channel_id, ftl->hmacBuffer)) != FTL_INGEST_RESP_OK) {
    FTL_LOG(ftl, FTL_LOG_ERROR, "ingest did not accept our authkey. Returned response code was %d", response_code);
    response_code = FTL_STREAM_REJECTED;
    goto fail;
  }

  /* We always send our version component first */
  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "ProtocolVersion: %d.%d", FTL_VERSION_MAJOR, FTL_VERSION_MINOR)) != FTL_INGEST_RESP_OK){
    response_code = FTL_OLD_VERSION;
    goto fail;
  }  

  /* Cool. Now ingest wants our stream meta-data, which we send as key-value pairs, followed by a "." */
  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VendorName: %s", ftl->vendor_name)) != FTL_INGEST_RESP_OK) {
	  goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VendorVersion: %s", ftl->vendor_version)) != FTL_INGEST_RESP_OK) {
	  goto fail;
  }

  ftl_video_component_t *video = &ftl->video;
  /* We're sending video */
  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "Video: true")) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VideoCodec: %s", ftl_video_codec_to_string(video->codec))) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VideoHeight: %d", video->height)) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VideoWidth: %d", video->width)) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VideoPayloadType: %d", video->media_component.payload_type)) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "VideoIngestSSRC: %d", video->media_component.ssrc)) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  ftl_audio_component_t *audio = &ftl->audio;

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "Audio: true")) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "AudioCodec: %s", ftl_audio_codec_to_string(audio->codec))) != FTL_INGEST_RESP_OK){
    goto fail;
  }    

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "AudioPayloadType: %d", audio->media_component.payload_type)) != FTL_INGEST_RESP_OK){
    goto fail;
  }    

  if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "AudioIngestSSRC: %d", audio->media_component.ssrc)) != FTL_INGEST_RESP_OK){
    goto fail;
  }                    

  if ( (response_code = _ftl_send_command(ftl, TRUE, response, sizeof(response), ".")) != FTL_INGEST_RESP_OK){
    goto fail;
  }

  /*see if there is a port specified otherwise use default*/
  int port = ftl_read_media_port(response);

  if (port < 0) {
	  ftl->media.assigned_port = FTL_UDP_MEDIA_PORT; //TODO: receive this from the server
  }
  else {
	  ftl->media.assigned_port = port;
  }

  FTL_LOG(ftl, FTL_LOG_INFO, "Successfully connected to ingest.  Media will be sent to port %d\n", ftl->media.assigned_port);

  ftl->connected = 1;
  
#ifdef _WIN32
  if ((ftl->connection_thread_handle = CreateThread(NULL, 0, connection_status_thread, ftl, 0, &ftl->connection_thread_id)) == NULL) {
#else
  if ((pthread_create(&ftl->connection_thread, NULL, connection_status_thread, ftl)) != 0) {
#endif
	  return FTL_MALLOC_FAILURE;
  }

  return FTL_SUCCESS;

fail:
  if (ftl->ingest_socket <= 0) {
    close_socket(ftl->ingest_socket);
  }

  response_code = _log_response(ftl, response_code);

  return response_code;
}

ftl_status_t _ingest_disconnect(ftl_stream_configuration_private_t *ftl) {

	ftl_response_code_t response_code = FTL_INGEST_RESP_UNKNOWN;
	char response[MAX_INGEST_COMMAND_LEN];

	if (ftl->connected) {
		ftl->connected = 0;

		//TODO: once light saber releases this legancy disconnect can go away
		if (is_legacy_ingest(ftl)) {
			FTL_LOG(ftl, FTL_LOG_INFO, "Legacy disconnect\n");
			/*TODO: we dont need a key to disconnect from a tcp connection*/
			if (!ftl_get_hmac(ftl->ingest_socket, ftl->key, ftl->hmacBuffer)) {
				FTL_LOG(ftl, FTL_LOG_ERROR, "could not get a signed HMAC!");
				response_code = FTL_INTERNAL_ERROR;
			}

			if ((response_code = _ftl_send_command(ftl, TRUE, response, sizeof(response), "DISCONNECT %d $%s", ftl->channel_id, ftl->hmacBuffer)) != FTL_INGEST_RESP_OK) {
				FTL_LOG(ftl, FTL_LOG_ERROR, "ingest did not accept our authkey. Returned response code was %d\n", response_code);
				response_code = response_code;
			}
		}
		else {
			FTL_LOG(ftl, FTL_LOG_INFO, "light-saber disconnect\n");
			if ((response_code = _ftl_send_command(ftl, FALSE, response, sizeof(response), "DISCONNECT", ftl->channel_id)) != FTL_INGEST_RESP_OK) {
				FTL_LOG(ftl, FTL_LOG_ERROR, "Ingest Disconnect failed with %d (%s)\n", response_code, response);
				response_code = response_code;
			}
		}
	}

	if (ftl->ingest_socket > 0) {
		close_socket(ftl->ingest_socket);
	}

	return FTL_SUCCESS;
}

static ftl_response_code_t _ftl_send_command(ftl_stream_configuration_private_t *ftl, BOOL need_response, char *response_buf, int response_len, const char *cmd_fmt, ...) {
  int resp_code = FTL_INGEST_RESP_OK;
  va_list valist;
  double sum = 0.0;
  char *buf = NULL;
  int len;
  int buflen = MAX_INGEST_COMMAND_LEN * sizeof(char);
  char *format = NULL;


  if( (buf = (char*)malloc(buflen)) == NULL){
    resp_code = FTL_INGEST_RESP_INTERNAL_MEMORY_ERROR;
	goto cleanup;
  }

  if( (format = (char*)malloc(strlen(cmd_fmt) + 5)) == NULL){
    resp_code = FTL_INGEST_RESP_INTERNAL_MEMORY_ERROR;
	goto cleanup;
  }

  sprintf_s(format, strlen(cmd_fmt) + 5, "%s\r\n\r\n", cmd_fmt);

  va_start(valist, cmd_fmt);

  memset(buf, 0, buflen);

  len = vsnprintf(buf, buflen, format, valist);

  va_end(valist);   

  if ( len < 0 || len >= buflen){
    resp_code = FTL_INGEST_RESP_INTERNAL_COMMAND_ERROR; 
    goto cleanup;
  }

  send(ftl->ingest_socket, buf, len, 0);

  if (need_response) {
    memset(response_buf, 0, response_len);
    len = recv_all(ftl->ingest_socket, response_buf, response_len, '\n');

    if (len < 0) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "ingest returned invalid response of %d\n", len);
      return FTL_INTERNAL_ERROR;
    }

    resp_code = ftl_read_response_code(response_buf);
  }
  
cleanup:
  if(buf != NULL){
    free(buf);
  }

  if(format != NULL){
      free(format);
  }

  return resp_code;
}

#ifdef _WIN32
DWORD WINAPI connection_status_thread(LPVOID data)
#else
static void *connection_status_thread(void *data)
#endif
{
	ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
	char buf;

	while (ftl->connected) {

		sleep_ms(500);

		int err = recv(ftl->ingest_socket, &buf, sizeof(buf), MSG_PEEK);

		if (err == 0 && ftl->connected) {
			ftl_status_t status_code;

			ftl->connected = 0;
			ftl->ready_for_media = 0;

			FTL_LOG(ftl, FTL_LOG_ERROR, "ingest connection has dropped: %s\n", get_socket_error());
			if ((status_code = _ingest_disconnect(ftl)) != FTL_SUCCESS) {
				FTL_LOG(ftl, FTL_LOG_ERROR, "Disconnect failed with error %d\n", status_code);
			}

			if ((status_code = media_destroy(ftl)) != FTL_SUCCESS) {
				FTL_LOG(ftl, FTL_LOG_ERROR, "failed to clean up media channel with error %d\n", status_code);
			}

			ftl_status_msg_t status;

			status.type = FTL_STATUS_EVENT;
			status.msg.event.reason = FTL_STATUS_EVENT_REASON_UNKNOWN;
			status.msg.event.type = FTL_STATUS_EVENT_TYPE_DISCONNECTED;

			enqueue_status_msg(ftl, &status);
			break;
		}

	}

	FTL_LOG(ftl, FTL_LOG_INFO, "Exited connection_status_thread\n");

	return 0;
}

ftl_status_t _log_response(ftl_stream_configuration_private_t *ftl, int response_code){
    switch (response_code) {
    case FTL_INGEST_RESP_OK:
      FTL_LOG(ftl, FTL_LOG_DEBUG, "ingest accepted our paramteres");
      break;
    case FTL_INGEST_RESP_BAD_REQUEST:
      FTL_LOG(ftl, FTL_LOG_ERROR, "ingest responded bad request. Possible charon bug?");
      return FTL_BAD_REQUEST;
    case FTL_INGEST_RESP_UNAUTHORIZED:
      FTL_LOG(ftl, FTL_LOG_ERROR, "channel is not authorized for FTL");
      return FTL_UNAUTHORIZED;
    case FTL_INGEST_RESP_OLD_VERSION:
      FTL_LOG(ftl, FTL_LOG_ERROR, "This version of the FTLSDK is depricated");
      return FTL_OLD_VERSION;
    case FTL_INGEST_RESP_AUDIO_SSRC_COLLISION:
      FTL_LOG(ftl, FTL_LOG_ERROR, "audio SSRC collision from this IP address. Please change your audio SSRC to an unused value");
      return FTL_INGEST_RESP_AUDIO_SSRC_COLLISION;
    case FTL_INGEST_RESP_VIDEO_SSRC_COLLISION:
      FTL_LOG(ftl, FTL_LOG_ERROR, "video SSRC collision from this IP address. Please change your audio SSRC to an unused value");
      return FTL_INGEST_RESP_VIDEO_SSRC_COLLISION;
    case FTL_INGEST_RESP_INTERNAL_SERVER_ERROR:
      FTL_LOG(ftl, FTL_LOG_ERROR, "parameters accepted, but ingest couldn't start FTL. Please contact support!");
      return FTL_INGEST_RESP_INTERNAL_SERVER_ERROR;
    case FTL_INGEST_RESP_INVALID_STREAM_KEY:
      FTL_LOG(ftl, FTL_LOG_ERROR, "invalid stream key or channel id");
      return FTL_STREAM_REJECTED;
  }

	return FTL_UNKNOWN_ERROR_CODE;
}
