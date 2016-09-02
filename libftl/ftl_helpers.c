/**
 * charon_protocol.c - Activates an FTL stream
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
 #include "hmac/hmac.h"

/*
    Please note that throughout the code, we send "\r\n\r\n", where a normal newline ("\n") would suffice.
    This is done due to some firewalls / anti-malware systems not passing our packets through when we don't send those double-windows-newlines.
    They seem to detect our protocol as HTTP wrongly.
*/

ftl_response_code_t ftl_read_response_code(const char * response_str) {
    char response_code_char[4];
    snprintf(response_code_char, 4, "%s", response_str);

    int response_code = atoi(response_code_char);

    /* Part of me feels like I've coded this stupidly */
    switch (response_code) {
        case FTL_INGEST_RESP_OK: /* Sucess */
            return FTL_INGEST_RESP_OK;
        case FTL_INGEST_RESP_BAD_REQUEST:
            return FTL_INGEST_RESP_BAD_REQUEST;
        case FTL_INGEST_RESP_UNAUTHORIZED:
            return FTL_INGEST_RESP_UNAUTHORIZED;
        case FTL_INGEST_RESP_OLD_VERSION:
            return FTL_INGEST_RESP_OLD_VERSION;
        case FTL_INGEST_RESP_AUDIO_SSRC_COLLISION:
            return FTL_INGEST_RESP_AUDIO_SSRC_COLLISION;
        case FTL_INGEST_RESP_VIDEO_SSRC_COLLISION:
            return FTL_INGEST_RESP_VIDEO_SSRC_COLLISION;
        case FTL_INGEST_RESP_INTERNAL_SERVER_ERROR:
            return FTL_INGEST_RESP_INTERNAL_SERVER_ERROR;
   }

   /* Got an invalid or unknown response code */
   return FTL_INGEST_RESP_UNKNOWN;
 }

unsigned char decode_hex_char(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    // Set the 5th bit. Makes ASCII chars lowercase :)
    c |= (1 << 5);
    
    if (c >= 'a' && c <= 'z') {
        return (c - 'a') + 10;
    }

    return 0;
}

int recv_all(int sock, char * buf, int buflen, const char line_terminator) {
    int pos = 0;
    int n;
    int bytes_recd = 0;

    do {
        n = recv(sock, buf, buflen, 0);
        if (n < 0) {
            //this will abort in the event of an error or in the buffer is filled before the terminiator is reached
            const char * error = ftl_get_socket_error();
            FTL_LOG(FTL_LOG_ERROR, "socket error while receiving: %s", error);
            return n;
        }
        
        buf += n;
        buflen -= n;
        bytes_recd += n;
    } while(buf[-1] != line_terminator);

    return bytes_recd;
}

int ftl_get_hmac(int sock, char * auth_key, char * dst) {
    char buf[2048];
    int string_len;
    int response_code;

    send(sock, "HMAC\r\n\r\n", 8, 0);
    string_len = recv_all(sock, buf, 2048, '\n');
    if (string_len < 4 || string_len == 2048) {
        FTL_LOG(FTL_LOG_ERROR, "ingest returned invalid response with length %d", string_len);
        return 0;
    }

    response_code = ftl_read_response_code(buf);
    if (response_code != FTL_INGEST_RESP_OK) {
        FTL_LOG(FTL_LOG_ERROR, "ingest did not give us an HMAC nonce");
        return 0;
    }

    int len = string_len - 5; // Strip "200 " and "\n"
    if (len % 2) {
        FTL_LOG(FTL_LOG_ERROR, "ingest did not give us a well-formed hex string");
        return 0;
    }

    int messageLen = len / 2;
    unsigned char *msg;

    if( (msg = (unsigned char*)malloc(messageLen * sizeof(*msg))) == NULL){
        FTL_LOG(FTL_LOG_ERROR, "Unable to allocate %d bytes of memory", messageLen * sizeof(*msg));
        return 0;        
    }

    int i;
    const char *hexMsgBuf = buf + 4;
    for(i = 0; i < messageLen; i++) {
        msg[i] = (decode_hex_char(hexMsgBuf[i * 2]) << 4) +
                  decode_hex_char(hexMsgBuf[(i * 2) + 1]);
    }

    hmacsha512(auth_key, msg, messageLen, dst);
    free(msg);
    return 1;
}

const char * ftl_audio_codec_to_string(ftl_audio_codec_t codec) {
  switch (codec) {
    case FTL_AUDIO_NULL: return "";
    case FTL_AUDIO_OPUS: return "OPUS";
    case FTL_AUDIO_AAC: return "AAC";
  }

  // Should be never reached
  return "";
}

const char * ftl_video_codec_to_string(ftl_video_codec_t codec) {
  switch (codec) {
    case FTL_VIDEO_NULL: return "";
    case FTL_VIDEO_VP8: return "VP8";
    case FTL_VIDEO_H264: return "H264";
  }

  // Should be never reached
  return "";
}