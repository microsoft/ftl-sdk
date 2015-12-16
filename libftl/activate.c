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
  hints.ai_flags = AI_ADDRCONFIG;
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
      FTL_LOG(FTL_LOG_DEBUG, "failed to create socket. error: %s", strerror(errno));
      continue;
    }

    /* Go for broke */
    if (connect (sock, p->ai_addr, p->ai_addrlen) == -1) {
      close(sock);
      sock = 0;
      FTL_LOG(FTL_LOG_DEBUG, "failed to connect on candidate, error: %s", strerror(errno));
      continue;
    }

    /* If we got here, we successfully connected */
    break;
  }

  /* Check to see if we actually connected */
  if (sock < 0) {
    FTL_LOG(FTL_LOG_ERROR, "failed to connect to ingest. Last error was: %s",
            strerror(errno));
    return FTL_CONNECT_ERROR;
  }

  /* If we've got a connection, let's send a CONNECT command and see if ingest will play ball */
  int string_len;

  string_len = snprintf(buf, 2048, "CONNECT %d %s\n", config->channel_id, config->authetication_key);
  if (string_len == 2048) {
    /* Abort, buffer exceeded */
    FTL_LOG(FTL_LOG_CRITICAL, "send buffer exceeded; connect string is too long!");
    close(sock);
    return FTL_INTERNAL_ERROR;
  }

  /* Send it, and let's read back the response */
  send(sock, buf, string_len, 0);
  recv(sock, buf, 2048, 0);

  response_code = ftl_charon_read_response_code(buf);
  if (response_code != FTL_CHARON_OK) {
    FTL_LOG(FTL_LOG_ERROR, "ingest did not accept our authkey. Returned response code was %d", response_code);
    close(sock);
    return FTL_STREAM_REJECTED;
  }

  /* Cool. Now ingest wants our stream meta-data, which we send as key-value pairs, followed by a "." */

  return FTL_SUCCESS;
}
