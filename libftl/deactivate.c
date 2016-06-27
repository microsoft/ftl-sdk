/**
 * deactivate.c - Deactivates an FTL stream
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

ftl_status_t ftl_deactivate_stream(ftl_stream_configuration_t *stream_config) {
  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
  ftl_charon_response_code_t response_code = FTL_CHARON_UNKNOWN;
  char disconnect_cmd[2048];
  char buf[2048];

  if (config->connected != 1) {
   return FTL_NOT_ACTIVE_STREAM;
  }

  char hmacBuffer[512];
  if(!ftl_charon_get_hmac(config->ingest_socket, config->authetication_key, hmacBuffer)) {
    FTL_LOG(FTL_LOG_ERROR, "could not get a signed HMAC!");
    return FTL_INTERNAL_ERROR;
  }
  int string_len;

  snprintf(disconnect_cmd, 2048, "DISCONNECT %d $%s\n", config->channel_id, hmacBuffer);
  send(config->ingest_socket, disconnect_cmd, strnlen(disconnect_cmd, 2048), 0);
  string_len = recv_all(config->ingest_socket, buf, 2048);

  if (string_len < 4 || string_len == 2048) {
    FTL_LOG(FTL_LOG_ERROR, "ingest returned invalid response with length %d", string_len);
    return FTL_INTERNAL_ERROR;
  }

  response_code = ftl_charon_read_response_code(buf);
  if (response_code != FTL_CHARON_OK) {
    FTL_LOG(FTL_LOG_ERROR, "ingest did not accept our disconnect. Returned response code was %d", response_code);
    return FTL_INTERNAL_ERROR;
  }

  ftl_close_socket(config->ingest_socket);
  config->connected = 0;

  return FTL_SUCCESS;
}
