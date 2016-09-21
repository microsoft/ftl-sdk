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
#include "win32\gettimeofday.h"
#else
#include <pthread.h>
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
#define SOCKET_RECV_TIMEOUT_MS 1000
#define SOCKET_SEND_TIMEOUT_MS 1000
#define MAX_PACKET_BUFFER 1500  //Max length of buffer
#define MAX_MTU 1392
#define FTL_UDP_MEDIA_PORT 8082   //The port on which to listen for incoming data
#define RTP_HEADER_BASE_LEN 12
#define RTP_FUA_HEADER_LEN 2
#define NACK_RB_SIZE 10240
#define NACK_RTT_AVG_SECONDS 5
#define MAX_STATUS_MESSAGE_QUEUED 10

#ifndef _WIN32
typdef SOCKET int
#endif

/*status message queue*/
typedef struct _status_queue_t {
	ftl_status_msg_t stats_msg;
	struct _status_queue_t *next;
}status_queue_elmt_t;

typedef struct {
	status_queue_elmt_t *head;
	int count;
#ifdef _WIN32
	HANDLE mutex;
	HANDLE sem;
#else
	pthread_mutex_t mutex;
#endif
}status_queue_t;

/**
 * This configuration structure handles basic information for a struct such
 * as the authetication keys and other similar information. It's members are
 * private and not to be directly manipulated
 */
typedef struct {
	uint8_t packet[MAX_PACKET_BUFFER];
	int len;
	struct timeval insert_time;
	int sn;
#ifdef _WIN32
	HANDLE mutex;
#else
	pthread_mutex_t mutex;
#endif
}nack_slot_t;

typedef struct {
	uint8_t payload_type;
	uint32_t ssrc;
	uint32_t timestamp;
	uint32_t timestamp_step;
	uint16_t seq_num;
	int64_t min_nack_rtt;
	int64_t max_nack_rtt;
	int64_t nack_rtt_avg;
	BOOL nack_slots_initalized;
	nack_slot_t *nack_slots[NACK_RB_SIZE];
	struct timeval stats;
}ftl_media_component_common_t;

typedef struct {
  ftl_audio_codec_t codec;
  ftl_media_component_common_t media_component;
} ftl_audio_component_t;

typedef struct {
  ftl_video_codec_t codec;
  uint32_t height;
  uint32_t width;
  float frame_rate;
  uint8_t fua_nalu_type;
  ftl_media_component_common_t media_component;
} ftl_video_component_t;

typedef struct {
	struct sockaddr_in server_addr;
	SOCKET media_socket;
	int assigned_port;
	BOOL recv_thread_running;
#ifdef _WIN32
	HANDLE recv_thread_handle;
	DWORD recv_thread_id;
#else
	pthread_t recv_thread;
#endif
	int max_mtu;
} ftl_media_config_t;

typedef struct {
  SOCKET ingest_socket;
  int connected;
  int ready_for_media;
  char ingest_ip[16];//ipv4 only
  uint32_t channel_id;
  char *key;
  char hmacBuffer[512];
#ifdef _WIN32
  HANDLE connection_thread_handle;
  DWORD connection_thread_id;
#else
  pthread_t connection_thread;
#endif
  ftl_media_config_t media;
  ftl_audio_component_t audio;
  ftl_video_component_t video;

  status_queue_t status_q;

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

int recv_all(SOCKET sock, char * buf, int buflen, const char line_terminator);

int ftl_get_hmac(SOCKET sock, char * auth_key, char * dst);
ftl_response_code_t ftl_read_response_code(const char * response_str);

/**
 * Platform abstractions
 **/

// FIXME: make this less global
extern char error_message[1000];

void ftl_init_sockets();
int ftl_close_socket(SOCKET sock);
char * ftl_get_socket_error();
int ftl_set_socket_recv_timeout(SOCKET socket, int ms_timeout);
int ftl_set_socket_send_timeout(SOCKET socket, int ms_timeout);
int ftl_set_socket_enable_keepalive(SOCKET socket);
int ftl_set_socket_send_buf(SOCKET socket, int buffer_space);
int dequeue_status_msg(ftl_stream_configuration_private_t *ftl, ftl_status_msg_t *stats_msg, int ms_timeout);
int enqueue_status_msg(ftl_stream_configuration_private_t *ftl, ftl_status_msg_t *stats_msg);

ftl_status_t _ingest_connect(ftl_stream_configuration_private_t *stream_config);
ftl_status_t _ingest_disconnect(ftl_stream_configuration_private_t *stream_config);

ftl_status_t media_init(ftl_stream_configuration_private_t *ftl);
ftl_status_t media_destroy(ftl_stream_configuration_private_t *ftl);
ftl_status_t media_send_video(ftl_stream_configuration_private_t *ftl, uint8_t *data, int32_t len, int end_of_frame);
ftl_status_t media_send_audio(ftl_stream_configuration_private_t *ftl, uint8_t *data, int32_t len);

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif

#endif
