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
#include "PhotonCommands.pb.h"
#include "hmac/hmac.h"
#include <stdarg.h>

OS_THREAD_ROUTINE  connection_status_thread(void *data);
OS_THREAD_ROUTINE  control_keepalive_thread(void *data);

static Photon::Commands::StatusCodes _ftl_start_connection(ftl_stream_configuration_private_t *ftl);
static Photon::Commands::StatusCodes _ftl_send_command(ftl_stream_configuration_private_t *ftl_cfg, const ::google::protobuf::Message& message);
static Photon::Commands::StatusCodes _ftl_recieve_command(ftl_stream_configuration_private_t *ftl, ::google::protobuf::Any& anyResponse);
static ftl_status_t __ingest_disconnect(ftl_stream_configuration_private_t *ftl, Photon::Commands::DisconnectReasons reason);
static ftl_status_t _translate_and_log_response(ftl_stream_configuration_private_t *ftl, Photon::Commands::StatusCodes response_code);

ftl_status_t _init_control_connection(ftl_stream_configuration_private_t *ftl) {

  int err = 0;
  SOCKET sock = 0;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  ftl_status_t retval = FTL_SUCCESS;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  struct addrinfo* resolved_names = 0;
  struct addrinfo* p = 0;

  int ingest_port = INGEST_PORT;
  char ingest_port_str[10];

  if (ftl_get_state(ftl, FTL_CONNECTED)) {
    return FTL_ALREADY_CONNECTED;
  }

  // Ensure the protobuf runtime and generated file versions match
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  snprintf(ingest_port_str, 10, "%d", ingest_port);

  if ((retval = _set_ingest_ip(ftl)) != FTL_SUCCESS) {
    return retval;
  }

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
  Photon::Commands::StatusCodes response_code = Photon::Commands::StatusCodes::UNKNOWN;

  int err = 0;
  char response[MAX_INGEST_COMMAND_LEN];

  if (ftl_get_state(ftl, FTL_CONNECTED)) {
    return FTL_ALREADY_CONNECTED;
  }

  if (ftl->ingest_socket <= 0) {
    return FTL_SOCKET_NOT_CONNECTED;
  }

  do {
    // Kick off the connection declaring our protocol.
    if ((response_code = _ftl_start_connection(ftl)) != Photon::Commands::StatusCodes::OK) {
      break;
    }

    // Send the connect message.
    Photon::Commands::Connect connect;
    connect.set_clientprotocolversion(Photon::Commands::ProtocolVersion::V1);
    if ((response_code = _ftl_send_command(ftl, connect)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to send connect command to ingest.");
      break;
    }

    // Wait for the response.
    ::google::protobuf::Any anyResponse;
    if ((response_code = _ftl_recieve_command(ftl, anyResponse)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to recieve connect response from ingest.");
      break;
    }

    Photon::Commands::Connect_Response connectResponse;
    if (!anyResponse.Is<Photon::Commands::Connect_Response>())
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Server returned an error from connect command.");
      break;
    }

    if (!anyResponse.UnpackTo(&connectResponse))
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to unpack connect response.");
      break;
    }

    // Grab the telemetry id, report it, and send it to the client.
    ftl->telemetryId = connectResponse.telemetryid();
    FTL_LOG(ftl, FTL_LOG_INFO, "Recieved telemetry Id from ingest. (%hs)", ftl->telemetryId.c_str());
    ftl_status_msg_t status;
    status.type = FTL_STATUS_TELEMETRY_ID;
    status.msg.telemetry_stats.telemetryId = ftl->telemetryId.c_str();
    status.msg.telemetry_stats.telemetryIdLen = ftl->telemetryId.length();
    enqueue_status_msg(ftl, &status);

    // Grab the encoded hmac and decode it.
    std::string encodedHmac = connectResponse.hmackey();    
    std::vector<byte> decodeBuffer;
    decodeBuffer.resize(encodedHmac.size() / 2);

    int i;
    const char *hexMsgBuf = encodedHmac.data();
    for (i = 0; i < decodeBuffer.size(); i++) {
      decodeBuffer.data()[i] = (decode_hex_char(hexMsgBuf[i * 2]) << 4) + decode_hex_char(hexMsgBuf[(i * 2) + 1]);
    }

    // Hash our key against the hmac
    hmacsha512(ftl->key, decodeBuffer.data(), decodeBuffer.size(), ftl->hashedKey);

    // Send the auth message
    Photon::Commands::Authenticate authenticate;
    authenticate.set_channelid(ftl->channel_id);
    authenticate.set_authkey(ftl->hashedKey);
    if ((response_code = _ftl_send_command(ftl, authenticate)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to send auth command to ingest.");
      break;
    }

    // Get the auth response.
    if ((response_code = _ftl_recieve_command(ftl, anyResponse)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to auth to ingest.");
      break;
    }

    Photon::Commands::Authenticate_Response authResponse;
    if (!anyResponse.Is<Photon::Commands::Authenticate_Response>())
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Server returned an error from authenticate command.");
      break;
    }

    if (!anyResponse.UnpackTo(&authResponse))
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to unpack authencate command response.");
      break;
    }

    // If we got here auth was successful!

    Photon::Commands::StartStream startStream;
    startStream.set_vendorname(ftl->vendor_name);
    startStream.set_vendorversion(ftl->vendor_version);
    startStream.set_hasvideo(true);

    ftl_video_component_t *video = &ftl->video;
    startStream.set_videocodec(ftl_video_codec_to_string(video->codec));
    startStream.set_videoheight(video->height);
    startStream.set_videowidth(video->width);
    startStream.set_videopayloadtype(video->media_component.payload_type);
    startStream.set_videoingestssrc(video->media_component.ssrc);

    ftl_audio_component_t *audio = &ftl->audio;
    startStream.set_audiocodec(ftl_audio_codec_to_string(audio->codec));
    startStream.set_audiopayloadtype(audio->media_component.payload_type);
    startStream.set_audioingestssrc(audio->media_component.ssrc);

    if ((response_code = _ftl_send_command(ftl, startStream)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to send stream start to ingest.");
      break;
    }

    // Wait for the start response.
    if ((response_code = _ftl_recieve_command(ftl, anyResponse)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to start the stream to ingest.");
      break;
    }

    Photon::Commands::StreamStart_Response startStreamResponse;
    if (!anyResponse.Is<Photon::Commands::StreamStart_Response>())
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Server returned an error from start stream command.");
      break;
    }

    if (!anyResponse.UnpackTo(&startStreamResponse))
    {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Unable to unpack start stream command response.");
      break;
    }

    // Set the ingest port
    ftl->media.assigned_port = startStreamResponse.ingestport();

    // We are ready! Kick of the threads to monitor the connection.
    ftl_set_state(ftl, FTL_CONNECTED);

    if (os_semaphore_create(&ftl->connection_thread_shutdown, "/ConnectionThreadShutdown", O_CREAT, 0) < 0) {
      response_code = Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
      break;
    }

    if (os_semaphore_create(&ftl->keepalive_thread_shutdown, "/KeepAliveThreadShutdown", O_CREAT, 0) < 0) {
      response_code = Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
      break;
    }

    if ((os_create_thread(&ftl->connection_thread, NULL, connection_status_thread, ftl)) != 0) {
      response_code = Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
      break;
    }

    if ((os_create_thread(&ftl->keepalive_thread, NULL, control_keepalive_thread, ftl)) != 0) {
      response_code = Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
      break;
    }

    FTL_LOG(ftl, FTL_LOG_INFO, "Successfully connected to ingest.  Media will be sent to port %d\n", ftl->media.assigned_port);
  
    return FTL_SUCCESS;
  } while (0);

  Photon::Commands::DisconnectReasons reason = response_code == Photon::Commands::StatusCodes::NO_RESPONSE ?
    Photon::Commands::DisconnectReasons::CLIENT_ERROR_TIMEOUT : Photon::Commands::DisconnectReasons::CLIENT_ERROR_BAD_RESPONSE;

  __ingest_disconnect(ftl, reason);

  return _translate_and_log_response(ftl, response_code);;
}

ftl_status_t _ingest_disconnect(ftl_stream_configuration_private_t *ftl, BOOL isClean)
{
  return __ingest_disconnect(ftl, isClean ? Photon::Commands::DisconnectReasons::CLIENT_CLEAN : Photon::Commands::DisconnectReasons::CLIENT_ERROR_UNKNOWN);
}

ftl_status_t __ingest_disconnect(ftl_stream_configuration_private_t *ftl, Photon::Commands::DisconnectReasons reason) {

  Photon::Commands::StatusCodes response_code = Photon::Commands::StatusCodes::UNKNOWN;
  char response[MAX_INGEST_COMMAND_LEN];

  if (ftl_get_state(ftl, FTL_KEEPALIVE_THRD)) {
    ftl_clear_state(ftl, FTL_KEEPALIVE_THRD);
    os_semaphore_post(&ftl->keepalive_thread_shutdown);
    os_wait_thread(ftl->keepalive_thread);
    os_destroy_thread(ftl->keepalive_thread);
    os_semaphore_delete(&ftl->keepalive_thread_shutdown);
  }

  if (ftl_get_state(ftl, FTL_CXN_STATUS_THRD)) {
    ftl_clear_state(ftl, FTL_CXN_STATUS_THRD);
    os_semaphore_post(&ftl->connection_thread_shutdown);
    os_wait_thread(ftl->connection_thread);
    os_destroy_thread(ftl->connection_thread);
    os_semaphore_delete(&ftl->connection_thread_shutdown);
  }

  if (ftl_get_state(ftl, FTL_CONNECTED)) {

    ftl_clear_state(ftl, FTL_CONNECTED);

    FTL_LOG(ftl, FTL_LOG_INFO, "light-saber disconnect\n");

    Photon::Commands::Disconnect disconnectCommand;
    disconnectCommand.set_reason(reason);
    if ((response_code = _ftl_send_command(ftl, disconnectCommand)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to disconnect command to ingest.");
    }
  }

  if (ftl->ingest_socket > 0) {
    close_socket(ftl->ingest_socket);
    ftl->ingest_socket = 0;
  }
  
  return FTL_SUCCESS;
}

static Photon::Commands::StatusCodes _ftl_send_command(ftl_stream_configuration_private_t *ftl, const ::google::protobuf::Message& message) {
  try
  {
    // Pack the message
    Photon::Commands::PhotonWrapper photonWrapper;
    photonWrapper.mutable_command()->PackFrom(message, "ftl-ingest");

    // Get the message size and fill the first 4 bits of the buffer with it.
    uint32_t messageSizeBytes = static_cast<uint32_t>(photonWrapper.ByteSizeLong());

    // Create a send buffer, size it, and add the size to the first 4 bytes.
    std::vector<char> sendBuffer;
    sendBuffer.resize(messageSizeBytes + 4);
    uint32_t* sendBufferUint32 = (uint32_t*)(sendBuffer.data());
    sendBufferUint32[0] = htonl(messageSizeBytes);

    // Write the protobuf object out.
    if (!photonWrapper.SerializeToArray((sendBuffer.data() + 4), sendBuffer.size() - 4))
    {
      return  Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
    }

    // Send the buffer.
    int sent = send(ftl->ingest_socket, sendBuffer.data(), sendBuffer.size(), 0);
    if (sent != sendBuffer.size())
    {
      return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
    }
  }
  catch (std::exception& ex)
  {
    return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
  }

  return Photon::Commands::StatusCodes::OK;
}

static Photon::Commands::StatusCodes _ftl_recieve_command(ftl_stream_configuration_private_t *ftl, ::google::protobuf::Any& anyResponse) {
  Photon::Commands::StatusCodes returnStatus = Photon::Commands::StatusCodes::UNKNOWN;

  try
  {
    // Pull the length off the wire.
    char sizeBuffer[4];
    int recSize = recv(ftl->ingest_socket, sizeBuffer, 4, MSG_WAITALL);
    if (recSize != 4)
    {
      return Photon::Commands::StatusCodes::NO_RESPONSE;
    }

    // Get the size
    uint32_t* recieveSizeBuf = (uint32_t*)(sizeBuffer);
    uint32_t recieveSize = ntohl(recieveSizeBuf[0]);

    // Now get the message buffer
    std::vector<char> messageBuff;
    messageBuff.resize(recieveSize);
    recSize = recv(ftl->ingest_socket, messageBuff.data(), messageBuff.size(), MSG_WAITALL);
    if (recSize != messageBuff.size())
    {
      return Photon::Commands::StatusCodes::NO_RESPONSE;
    }

    // Now unpack the message.
    Photon::Commands::PhotonWrapper photonWrapper;
    if (!photonWrapper.ParseFromArray(messageBuff.data(), messageBuff.size()))
    {
      return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
    }

    // Set the return code
    returnStatus = photonWrapper.statuscode();

    // Unpack the any if there is one
    if (photonWrapper.has_command())
    {
      anyResponse.CopyFrom(photonWrapper.command());
    }
  }
  catch (std::exception& ex)
  {
    return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
  }

  return returnStatus;
}

static Photon::Commands::StatusCodes _ftl_start_connection(ftl_stream_configuration_private_t *ftl) {

  try
  {
    // To start the connection we send the protocol name. This is how we will determine
    // what we are going to speak.
    std::string buff = "photon\n";
    int sent = send(ftl->ingest_socket, buff.c_str(), buff.size(), 0);
    if (sent != buff.size())
    {
      return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
    }
  }
  catch (std::exception& ex)
  {
    return Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR;
  }

  return Photon::Commands::StatusCodes::OK;
}

OS_THREAD_ROUTINE control_keepalive_thread(void *data)
{
  ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
  Photon::Commands::StatusCodes response_code;

  ftl_set_state(ftl, FTL_KEEPALIVE_THRD);

  while (ftl_get_state(ftl, FTL_KEEPALIVE_THRD)) {
    os_semaphore_pend(&ftl->keepalive_thread_shutdown, KEEPALIVE_FREQUENCY_MS);

    if (!ftl_get_state(ftl, FTL_KEEPALIVE_THRD))
    {
      break;
    }

    // Send a heartbeat
    Photon::Commands::Heartbeat heartbeat;
    if ((response_code = _ftl_send_command(ftl, heartbeat)) != Photon::Commands::StatusCodes::OK) {
      FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to send heatbeat command to ingest.");
      break;
    }
  }

  FTL_LOG(ftl, FTL_LOG_INFO, "Exited control_keepalive_thread\n");

  return 0;
}

OS_THREAD_ROUTINE connection_status_thread(void *data)
{
  ftl_stream_configuration_private_t *ftl = (ftl_stream_configuration_private_t *)data;
  char buf[1024];
  ftl_status_msg_t status;
  struct timeval last_ping, now;
  int ms_since_ping = 0;
  int keepalive_is_late = KEEPALIVE_FREQUENCY_MS + KEEPALIVE_FREQUENCY_MS; // Add a 5s buffer to the wait time

  gettimeofday(&last_ping, NULL);

  ftl_set_state(ftl, FTL_CXN_STATUS_THRD);

  while (ftl_get_state(ftl, FTL_CXN_STATUS_THRD)) {

    os_semaphore_pend(&ftl->keepalive_thread_shutdown, 500);
    if (!ftl_get_state(ftl, FTL_CXN_STATUS_THRD))
    {
      break;
    }

    // Check to see if there is anything on the socket to read.
    int ret = recv(ftl->ingest_socket, buf, sizeof(buf), MSG_PEEK);

    gettimeofday(&now, NULL);
    ms_since_ping = timeval_subtract_to_ms(&now, &last_ping);

    if (ret == 0 && ftl_get_state(ftl, FTL_CXN_STATUS_THRD) || ret > 0 || ms_since_ping > keepalive_is_late) {
      ftl_status_t error_code = FTL_SUCCESS;

      if (ret > 0) {
        // Get the data on the socket.
        ::google::protobuf::Any anyResponse;
        Photon::Commands::StatusCodes returnStatus;
        if ((returnStatus = _ftl_recieve_command(ftl, anyResponse)) != Photon::Commands::StatusCodes::OK) {
          FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to read command from ingest.");
          error_code = _translate_and_log_response(ftl, returnStatus);
          break;
        }

        // If it is a heartbeat response take the current time and continue.
        if (anyResponse.Is<Photon::Commands::Heartbeat_Response>())
        {
          gettimeofday(&last_ping, NULL);
          continue;
        }
        // If it is a disconnect message handle it correctly.
        else if(anyResponse.Is<Photon::Commands::Disconnect>())
        {
          error_code = FTL_USER_DISCONNECT;
          Photon::Commands::Disconnect disconnect;
          if (anyResponse.UnpackTo(&disconnect))
          {
            FTL_LOG(ftl, FTL_LOG_INFO, "Disconnect message recieved from ingest. Reason (%d)", disconnect.reason());
          }
          else
          {
            FTL_LOG(ftl, FTL_LOG_ERROR, "Failed to parse disconnect message from server.");
          }
        }
        // For unknown messages print and ignore them.
        else
        {           
          FTL_LOG(ftl, FTL_LOG_WARN, "Unknown command sent from ingest. (%hs)", anyResponse.GetTypeName().c_str());
          continue;
        }   
      }

      if (ms_since_ping > keepalive_is_late) {
        error_code = FTL_NO_PING_RESPONSE;
      }
      
      FTL_LOG(ftl, FTL_LOG_ERROR, "ingest connection has dropped: %s\n", get_socket_error());

      ftl_clear_state(ftl, FTL_CXN_STATUS_THRD);

      if (os_trylock_mutex(&ftl->disconnect_mutex)) {
        internal_ingest_disconnect(ftl, FALSE);
        os_unlock_mutex(&ftl->disconnect_mutex);
      }

      status.type = FTL_STATUS_EVENT;
      if (error_code == FTL_NO_MEDIA_TIMEOUT) {
        status.msg.event.reason = FTL_STATUS_EVENT_REASON_NO_MEDIA;
      }
      else {
        status.msg.event.reason = FTL_STATUS_EVENT_REASON_UNKNOWN;
      }
      status.msg.event.type = FTL_STATUS_EVENT_TYPE_DISCONNECTED;
      status.msg.event.error_code = error_code;
      enqueue_status_msg(ftl, &status);
      break;
    }
  }

  FTL_LOG(ftl, FTL_LOG_INFO, "Exited connection_status_thread\n");

  return 0;
}

static ftl_status_t _translate_and_log_response(ftl_stream_configuration_private_t *ftl, Photon::Commands::StatusCodes response_code){

    switch (response_code) {

    case Photon::Commands::StatusCodes::OK:
      return FTL_SUCCESS;
    case Photon::Commands::StatusCodes::PING:
      return FTL_SUCCESS;

    case Photon::Commands::StatusCodes::NO_RESPONSE:
      FTL_LOG(ftl, FTL_LOG_ERROR, "ingest did not respond to request");
      return FTL_INGEST_NO_RESPONSE;
    case Photon::Commands::StatusCodes::BAD_REQUEST:
      FTL_LOG(ftl, FTL_LOG_ERROR, "ingest responded bad request");
      return FTL_BAD_REQUEST;
    case Photon::Commands::StatusCodes::UNAUTHORIZED:
      FTL_LOG(ftl, FTL_LOG_ERROR, "channel is not authorized for FTL");
      return FTL_UNAUTHORIZED;
    case Photon::Commands::StatusCodes::OLD_VERSION:
      FTL_LOG(ftl, FTL_LOG_ERROR, "This version of the FTLSDK is depricated");
      return FTL_OLD_VERSION;
    case Photon::Commands::StatusCodes::INVALID_STREAM_KEY:
      FTL_LOG(ftl, FTL_LOG_ERROR, "The stream key or channel id is incorrect");
      return FTL_BAD_OR_INVALID_STREAM_KEY;
    case Photon::Commands::StatusCodes::CHANNEL_IN_USE:
      FTL_LOG(ftl, FTL_LOG_ERROR, "the channel id is already actively streaming");
      return FTL_CHANNEL_IN_USE;
    case Photon::Commands::StatusCodes::REGION_UNSUPPORTED:
      FTL_LOG(ftl, FTL_LOG_ERROR, "the region is not authorized to stream");
      return FTL_REGION_UNSUPPORTED;
    case Photon::Commands::StatusCodes::NO_MEDIA_TIMEOUT:
      FTL_LOG(ftl, FTL_LOG_ERROR, "The server did not receive media (audio or video) for an extended period of time");
      return FTL_NO_MEDIA_TIMEOUT;
    case Photon::Commands::StatusCodes::INTERNAL_SERVER_ERROR:
      FTL_LOG(ftl, FTL_LOG_ERROR, "parameters accepted, but ingest couldn't start FTL. Please contact support!");
      return FTL_INTERNAL_ERROR;
    case Photon::Commands::StatusCodes::INTERNAL_COMMAND_ERROR:
      FTL_LOG(ftl, FTL_LOG_ERROR, "Server memory error");
      return FTL_INTERNAL_ERROR;
    case Photon::Commands::StatusCodes::INTERNAL_LOCAL_ERROR:
      FTL_LOG(ftl, FTL_LOG_ERROR, "Internal local error");
      return FTL_INTERNAL_ERROR;      
  }

  return FTL_UNKNOWN_ERROR_CODE;
}
