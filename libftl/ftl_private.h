/**
 * \file ftl_private.h - Private Interfaces for the FTL SDK
 *
 * Copyright (c) 2015 Beam Inc.
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

 #ifndef __FTL_PRIVATE_H
 #define __FTL_PRIVATE_H

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <WS2tcpip.h>
#include <WinSock2.h>
#endif

#ifndef _WIN32
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#define MAX_INGEST_COMMAND_LEN 512
#define INGEST_PORT 8084
#define MAX_KEY_LEN 100
#define VIDEO_PTYPE 96
#define AUDIO_PTYPE 97
#define SOCKET_RECV_TIMEOUT_MS 500
#define SOCKET_SEND_TIMEOUT_MS 500
/**
 * This configuration structure handles basic information for a struct such
 * as the authetication keys and other similar information. It's members are
 * private and not to be directly manipulated
 */
typedef struct {
  ftl_audio_codec_t codec;
  uint8_t payload_type;
  uint32_t ssrc;
  void* codec_info;
} ftl_stream_audio_component_private_common_t;

typedef struct {
  ftl_video_codec_t codec;
  uint8_t payload_type;
  uint32_t ssrc;
  uint32_t height;
  uint32_t width;
  void* codec_info;
} ftl_stream_video_component_private_common_t;

typedef struct {
  int ingest_socket;
  int connected;
  char ingest_ip[16];//ipv4 only
  uint32_t channel_id;
  char *key;
  ftl_stream_audio_component_private_common_t audio;
  ftl_stream_video_component_private_common_t video;
}  ftl_stream_configuration_private_t;

/**
 * Charon always responses with a three digit response code after each command
 *
 * This enum holds defined number sequences
 **/

typedef enum {
  FTL_INGEST_RESP_UNKNOWN = 0,
  FTL_INGEST_RESP_OK = 200,
  FTL_INGEST_RESP_BAD_REQUEST= 400,
  FTL_INGEST_RESP_UNAUTHORIZED = 401,
  FTL_INGEST_RESP_OLD_VERSION = 402,
  FTL_INGEST_RESP_AUDIO_SSRC_COLLISION = 403,
  FTL_INGEST_RESP_VIDEO_SSRC_COLLISION = 404,
  FTL_INGEST_RESP_INVALID_STREAM_KEY = 405,
  FTL_INGEST_RESP_INTERNAL_SERVER_ERROR = 500,
  FTL_INGEST_RESP_INTERNAL_MEMORY_ERROR = 900,
  FTL_INGEST_RESP_INTERNAL_COMMAND_ERROR = 901
} ftl_response_code_t;

/**
 * Logs something to the FTL logs
 */

#define FTL_LOG(log_level, ...) ftl_log_message (log_level, __FILE__, __LINE__, __VA_ARGS__);
void ftl_logging_init(); /* Sets the callback to 0 disabling it */
void ftl_log_message(ftl_log_severity_t log_level, const char * file, int lineno, const char * fmt, ...);

/**
 * Value to string conversion functions
 */

const char * ftl_audio_codec_to_string(ftl_audio_codec_t codec);
const char * ftl_video_codec_to_string(ftl_video_codec_t codec);

/**
 * Functions related to the charon prootocol itself
 **/

int recv_all(int sock, char * buf, int buflen, const char line_terminator);

int ftl_get_hmac(int sock, char * auth_key, char * dst);
ftl_response_code_t ftl_read_response_code(const char * response_str);

/**
 * Platform abstractions
 **/

// FIXME: make this less global
extern char error_message[1000];

void ftl_init_sockets();
int ftl_close_socket(int sock);
char * ftl_get_socket_error();

ftl_status_t _ingest_connect(ftl_stream_configuration_private_t *stream_config);

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif

#endif
