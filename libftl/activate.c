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

ftl_status_t ftl_activate_stream(ftl_stream_configuration_t *stream_config) {
  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
  ftl_charon_response_code_t response_code = FTL_CHARON_UNKNOWN;
  char buf[2048];

  /* Let's validate that we have everything that we need */
  if (config->authetication_key == 0 ||
      config->channel_id == 0) {
        FTL_LOG(FTL_LOG_CRITICAL, "unable to activate, missing information")
        return FTL_CONFIG_ERROR;
  }

  /* First things first, resolve ingest IP address */
  int err = 0;
  int sock = 0;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  struct addrinfo* resolved_names = 0;
  struct addrinfo* p = 0;

  /* FIXME: dehardcode the port */
  int ingest_port = 8084;
  char ingest_port_str[10];
  snprintf(ingest_port_str, 10, "%d", ingest_port);
  
  err = getaddrinfo(config->ingest_location, ingest_port_str, &hints, &resolved_names);
  if (err != 0) {
    FTL_LOG(FTL_LOG_ERROR, "getaddrinfo failed to look up ingest address %s.", config->ingest_location);
    FTL_LOG(FTL_LOG_ERROR, "gai error was: %s", gai_strerror(err));
    return FTL_DNS_FAILURE;
  }
  
  /* Open a socket to the control port */
  for (p = resolved_names; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == -1) {
      /* try the next candidate */
      FTL_LOG(FTL_LOG_DEBUG, "failed to create socket. error: %s", ftl_get_socket_error());
      continue;
    }

    /* Go for broke */
    if (connect (sock, p->ai_addr, p->ai_addrlen) == -1) {
      FTL_LOG(FTL_LOG_DEBUG, "failed to connect on candidate, error: %s", ftl_get_socket_error());
      ftl_close_socket(sock);
      sock = 0;
      continue;
    }

    /* If we got here, we successfully connected */
    break;
  }

  /* Free the resolved name struct */
  freeaddrinfo(resolved_names);
  
  /* Check to see if we actually connected */
  if (sock <= 0) {
    FTL_LOG(FTL_LOG_ERROR, "failed to connect to ingest. Last error was: %s",
            ftl_get_socket_error());
    return FTL_CONNECT_ERROR;
  }

  int string_len;

  char hmacBuffer[512];
  if(!ftl_charon_get_hmac(sock, config->authetication_key, hmacBuffer)) {
    FTL_LOG(FTL_LOG_ERROR, "could not get a signed HMAC!");
    ftl_close_socket(sock);
    return FTL_INTERNAL_ERROR;
  }

  /* If we've got a connection, let's send a CONNECT command and see if ingest will play ball */

  string_len = snprintf(buf, 2048, "CONNECT %d $%s\r\n\r\n", config->channel_id, hmacBuffer);
  if (string_len == 2048) {
    /* Abort, buffer exceeded */
    FTL_LOG(FTL_LOG_CRITICAL, "send buffer exceeded; connect string is too long!");
    ftl_close_socket(sock);
    return FTL_INTERNAL_ERROR;
  }
  
  /* Send it, and let's read back the response */
  send(sock, buf, string_len, 0);
  string_len = recv_all(sock, buf, 2048);

  if (string_len < 4 || string_len == 2048) {
    FTL_LOG(FTL_LOG_ERROR, "ingest returned invalid response with length %d", string_len);
    ftl_close_socket(sock);
    return FTL_INTERNAL_ERROR;
  }

  response_code = ftl_charon_read_response_code(buf);
  if (response_code != FTL_CHARON_OK) {
    FTL_LOG(FTL_LOG_ERROR, "ingest did not accept our authkey. Returned response code was %d", response_code);
    ftl_close_socket(sock);
    return FTL_STREAM_REJECTED;
  }

  /* We always send our version component first */
  string_len = snprintf(buf, 2048, "ProtocolVersion: %d.%d\r\n\r\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR);
  send(sock, buf, string_len, 0);

  /* Cool. Now ingest wants our stream meta-data, which we send as key-value pairs, followed by a "." */
  ftl_stream_video_component_private_common_t *video_component = config->video_component->private;
  if (video_component != 0) {
    /* We're sending video */
    const char video_true[] = "Video: true\r\n\r\n";
    send(sock, video_true, strlen(video_true), 0);

    /* FIXME: make this a macro or a function? */
    string_len = snprintf(buf, 2048, "VideoCodec: %s\r\n\r\n", ftl_video_codec_to_string(video_component->codec));
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "VideoHeight: %d\r\n\r\n", video_component->height);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "VideoWidth: %d\r\n\r\n", video_component->width);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "VideoPayloadType: %d\r\n\r\n", video_component->payload_type);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "VideoIngestSSRC: %d\r\n\r\n", video_component->ssrc);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);
  }

  ftl_stream_audio_component_private_common_t *audio_component = config->audio_component->private;
  if (audio_component != 0) {
    /* We're sending video */
    const char audio_true[] = "Audio: true\r\n\r\n";
    send(sock, audio_true, strlen(audio_true), 0);

    string_len = snprintf(buf, 2048, "AudioCodec: %s\r\n\r\n", ftl_audio_codec_to_string(audio_component->codec));
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "AudioPayloadType: %d\r\n\r\n", audio_component->payload_type);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);

    string_len = snprintf(buf, 2048, "AudioIngestSSRC: %d\r\n\r\n", audio_component->ssrc);
    if (string_len == 2048)  goto buffer_overflow;
    send(sock, buf, string_len, 0);
  }
  /* Ok, we're done sending parameters, send the finished signal */
  const char conclude_signal[] = ".\r\n\r\n";
  send(sock, conclude_signal, strlen(conclude_signal), 0);

  // Check our return code
  string_len = recv_all(sock, buf, 2048);

  if (string_len < 4 || string_len == 2048) {
    FTL_LOG(FTL_LOG_ERROR, "ingest returned invalid response with length %d", string_len);
    ftl_close_socket(sock);
    return FTL_INTERNAL_ERROR;
  }

  response_code = ftl_charon_read_response_code(buf);
  switch (response_code) {
    case FTL_CHARON_OK:
      FTL_LOG(FTL_LOG_DEBUG, "ingest accepted our paramteres");
      break;
    case FTL_CHARON_BAD_REQUEST:
      FTL_LOG(FTL_LOG_ERROR, "ingest responded bad request. Possible charon bug?");
      return FTL_BAD_REQUEST;
    case FTL_CHARON_UNAUTHORIZED:
      FTL_LOG(FTL_LOG_ERROR, "channel is not authorized for FTL");
      return FTL_UNAUTHORIZED;
    case FTL_CHARON_OLD_VERSION:
      FTL_LOG(FTL_LOG_ERROR, "charon protocol mismatch. Please update to latest charon/libftl");
      return FTL_OLD_VERSION;
    case FTL_CHARON_AUDIO_SSRC_COLLISION:
      FTL_LOG(FTL_LOG_ERROR, "audio SSRC collision from this IP address. Please change your audio SSRC to an unused value");
      return FTL_CHARON_AUDIO_SSRC_COLLISION;
    case FTL_CHARON_VIDEO_SSRC_COLLISION:
      FTL_LOG(FTL_LOG_ERROR, "video SSRC collision from this IP address. Please change your audio SSRC to an unused value");
      return FTL_CHARON_VIDEO_SSRC_COLLISION;
    case FTL_CHARON_INTERNAL_SERVER_ERROR:
      FTL_LOG(FTL_LOG_ERROR, "parameters accepted, but ingest couldn't start FTL. Please contact support!");
      return FTL_CHARON_INTERNAL_SERVER_ERROR;
    case FTL_CHARON_INVALID_STREAM_KEY:
      FTL_LOG(FTL_LOG_ERROR, "invalid stream key or channel id");
      return FTL_STREAM_REJECTED;
  }

  // We're good to go, set the connected status to true, and save the socket
  config->connected = 1;
  config->ingest_socket = sock;
  return FTL_SUCCESS;

buffer_overflow:
  FTL_LOG(FTL_LOG_CRITICAL, "internal buffer overflow! Bailing out!");
  if (sock <= 0) ftl_close_socket(sock);
  return FTL_INTERNAL_ERROR;
}
