/**
 * \file ftl.h - Private Interfaces for the FTL SDK
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

/**
 * This configuration structure handles basic information for a struct such
 * as the authetication keys and other similar information. It's members are
 * private and not to be directly manipulated
 */

typedef struct {
  int ingest_socket;
  int connected;
  char * ingest_location;
  uint32_t channel_id;
  char * authetication_key;
  ftl_stream_audio_component_t* audio_component;
  ftl_stream_video_component_t* video_component;
}  ftl_stream_configuration_private_t;

typedef struct {
  ftl_video_codec_t codec;
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

typedef enum {
  FTL_LOG_CRITICAL,
  FTL_LOG_ERROR,
  FTL_LOG_WARN,
  FTL_LOG_INFO,
  FTL_LOG_DEBUG
} ftl_log_severity_t;

/**
 * Charon always responses with a three digit response code after each command
 *
 * This enum holds defined number sequences
 **/

typedef enum {
  FTL_CHARON_UNKNOWN = 0,
  FTL_CHARON_OK = 200
} ftl_charon_response_code_t;

/**
 * Logs something to the FTL logs
 */

#define FTL_LOG(log_level, ...) ftl_log_message (log_level, __FILE__, __LINE__, __VA_ARGS__);
void ftl_log_message(ftl_log_severity_t log_level, const char * file, int lineno, const char * fmt, ...);

/**
 * Value to string conversion functions
 */

const char * ftl_audio_codec_to_string(ftl_video_codec_t codec);
const char * ftl_video_codec_to_string(ftl_video_codec_t codec);

/**
 * Functions related to the charon prootocol itself
 **/

ftl_charon_response_code_t ftl_charon_read_response_code(const char * response_str);

/**
 * Platform abstractions
 **/

void ftl_init_sockets();
int ftl_close_socket(int sock);

#endif
