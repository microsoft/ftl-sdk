/**
 * \file ftl.h - Public Interface for the FTL SDK
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

#ifndef __FTL_H
#define __FTL_H

#include <stdint.h>

#ifdef _WIN32
#	ifdef __FTL_INTERNAL
#		define FTL_API __declspec(dllexport)
#	else
#		define FTL_API __declspec(dllimport)
#	endif
#else
#	define FTL_API
#endif

FTL_API extern const int FTL_VERSION_MAJOR;
FTL_API extern const int FTL_VERSION_MINOR;
FTL_API extern const int FTL_VERSION_MAINTENANCE;

/*! \defgroup ftl_public Public Interfaces for libftl */

/*! \brief Status codes used by libftl
 *  \ingroup ftl_public
 */

typedef enum {
  FTL_SUCCESS, /**< Operation was successful */
  FTL_NON_ZERO_POINTER, /**< Function required a zero-ed pointer, but didn't get one */
  FTL_MALLOC_FAILURE, /**< memory allocation failed */
  FTL_DNS_FAILURE, /**< DNS probe failed */
  FTL_CONNECT_ERROR, /**< Failed to connect to ingest */
  FTL_INTERNAL_ERROR, /**< Got valid inputs, but FTL failed to complete the action due to internal failure */
  FTL_CONFIG_ERROR, /**< The configuration supplied was invalid or incomplete */
  FTL_STREAM_REJECTED, /**< Ingest rejected our connect command */
  FTL_NOT_ACTIVE_STREAM, /**< The function required an active stream and was passed an inactive one */
  FTL_UNAUTHORIZED, /**< Parameters were correct, but streamer not authorized to use FTL */
  FTL_AUDIO_SSRC_COLLISION, /**< The audio SSRC from this IP is currently in use */
  FTL_VIDEO_SSRC_COLLISION, /**< The video SSRC from this IP is currently in use */
  FTL_BAD_REQUEST, /**< Ingest didn't like our request. Should never happen */
  FTL_OLD_VERSION /**< libftl needs to be updated */
} ftl_status_t;

/*! \brief Video codecs supported by FTL
 *  \ingroug ftl_public
 */

typedef enum {
  FTL_VIDEO_NULL, /**< No video for this stream */
  FTL_VIDEO_VP8,  /**< Google's VP8 codec (recommended default) */
  FTL_VIDEO_H264
} ftl_video_codec_t;

/*! \brief Audio codecs supported by FTL
 *  \ingroup ftl_public
 */

typedef enum {
  FTL_AUDIO_NULL, /**< No audio for this stream */
  FTL_AUDIO_OPUS, /**< Xiph's Opus audio codec */
  FTL_AUDIO_AAC
} ftl_audio_codec_t;

/*! \brief Log levels used by libftl; returned via logging callback
 *  \ingroup ftl_public
 */

typedef enum {
  FTL_LOG_CRITICAL,
  FTL_LOG_ERROR,
  FTL_LOG_WARN,
  FTL_LOG_INFO,
  FTL_LOG_DEBUG
} ftl_log_severity_t;

/*! \brief Configuration information for a given stream
 *  \ingroup ftl_public
 *
 * The members of this structure are private
 */

typedef struct {
  void* private;
} ftl_stream_configuration_t;

/*! \brief Configuration information for a audio stream component
 *  \ingroup ftl_public
 *
 * The members of this structure are private
 */

typedef struct {
  void* private;
} ftl_stream_audio_component_t;

/*! \brief Configuration information for a video stream component
 *  \ingroup ftl_public
 *
 * The members of this structure are private
 */

typedef struct {
  void* private;
} ftl_stream_video_component_t;

/*! \brief Function prototype for FTL logging callback
 * \ingroup ftl_public
 */

typedef void (*ftl_logging_function_t)(ftl_log_severity_t log_level, const char * log_message);

/*!
 * \ingroup ftl_public
 * \brief FTL Initialization
 *
 * Before using FTL, you must call ftl_init before making any additional calls
 * in this library. ftl_init initializes any submodules that FTL depends on such
 * as libsrtp. Under normal cirmstances, this function should never fail.
 *
 * On Windows, this function calls WSAStartup() to initialize Winsock. It is
 * the responsibility of the calling app to call WSACleanup() at application
 * shutdown as FTL can't safely call it (as your application may be using sockets
 * elsewhere
 *
 * @returns FTL_INIT_SUCCESS on successful initialization. Otherwise, returns
 * ftl_init_status_t enum with the failure state.
 */
FTL_API ftl_status_t ftl_init();

/*!
 * \ingroup ftl_public
 * \brief Initializes a stream configuration structure
 *
 * Allocates memory for a given stream, and allows it to be manipulated by
 * other API calls. This must be called once per stream. The pointer *must*
 * be derferenced and initialized to 0 before passing it to ftl_create_stream.
 *
 * @returns FTL_SUCCESS if the stream configuration tables were successfully
 * initialized
 * @returns FTL_NON_ZERO_POINTER if the pointer passed wasn't zeroed before hand
 */

FTL_API ftl_status_t ftl_create_stream_configuration(ftl_stream_configuration_t** stream_config);

/*!
 * \ingroup ftl_public
 * \brief Set ingest location
 *
 * Sets the ingest hostname for where this FTL stream is going.
 *
 * @param stream_config An initialized stream_configuration struct
 * @param ingest_location A standard null-terminated C string with the hostname or IP address of ingest
 **/

FTL_API void ftl_set_ingest_location(ftl_stream_configuration_t *stream_config, const char * ingest_location);

/*!
 * \ingroup ftl_public
 * \brief Set authetication key
 *
 * Authetication keys are used by beam ingest. These keys are shown in the UI
 * of beam.pro
 *
 * @param stream_config An initialized stream_configuration struct
 * @param channel_id the numerical channel assigned to the user by the beam backend
 * @param authetication_key A standard null-terminated C string with the user-specific authetication key
 **/

FTL_API void ftl_set_authetication_key(ftl_stream_configuration_t *stream_config, uint32_t channel_id, const char * auth_key);

/*!
 * \ingroup ftl_public
 * \brief Creates an audio component object to add to a stream.
 *
 * FTL streams are comprised of multiple components. In most cases, the two
 * present components are audio and video, of which there can be one of each.
 * ftl_create_audiocomponent creates a structure describing the audio component
 * and allows configuration of it
 *
 * @param codec - video codec supported by FTL
 * @param payload_type - RTP payload type of the stream. May be set to NULL to choose a sensible default
 * @param ssrc - SSRC identifer of the stream. May be set to NULL to dynamically generate a SSRC
 */

FTL_API ftl_stream_audio_component_t* ftl_create_audio_component(ftl_audio_codec_t codec, uint8_t payload_type, uint32_t ssrc);

/*!
 * \ingroup ftl_public
 * \brief Creates a video component object to add to a stream.
 *
 * FTL streams are comprised of multiple components. In most cases, the two
 * present components are audio and video, of which there can be one of each.
 * ftl_video_create_component creates a structure describing the video component
 * and allows configuration of it
 *
 * @param codec - video codec supported by FTL
 * @param payload_type - RTP payload type of the stream. May be set to NULL to choose a sensible default
 * @param ssrc - SSRC identifer of the stream. May be set to NULL to dynamically generate a SSRC
 * @param height - Height of the video to be encoded
 * @param width - Width of the video to be encoded
 */

FTL_API ftl_stream_video_component_t* ftl_create_video_component(ftl_video_codec_t codec, uint8_t payload_type, uint32_t ssrc, uint32_t width, uint32_t height);

/*!
* \ingroup ftl_public
* \brief Attachs a audio component to the stream
*
* After an audio component is configured, it must be attached to the stream. This
* function does just that.
*
* @param stream_config An initialized stream configuration struct
* @param audio_component An initialized video component
**/

FTL_API void ftl_attach_audio_component_to_stream(ftl_stream_configuration_t* stream_config, ftl_stream_audio_component_t* component);

/*!
 * \ingroup ftl_public
 * \brief Attachs a video component to the stream
 *
 * After a video component is configured, it must be attached to the stream. This
 * function does just that.
 *
 * @param stream_config An initialized stream configuration struct
 * @param video_component An initialized video component
 **/

FTL_API void ftl_attach_video_component_to_stream(ftl_stream_configuration_t* stream_config, ftl_stream_video_component_t* component);

/*!
 * \ingroup ftl_public
 * \brief Activates FTL
 *
 * FTL streams are marked "online" by holding a connection open to styx. This
 * function uses the information set in the stream configuration to notify beam
 * that a stream is online, and the configuration aspects of said stream. This
 * function returns after a stream is accepted by ingest, and starts a keepalive
 * thread to hold it open.
 *
 * @param stream_config Stream configuration information
 */

FTL_API ftl_status_t ftl_activate_stream(ftl_stream_configuration_t *stream_config);

/*!
 * \ingroup ftl_public
 * \brief Deactivates an FTL stream
 *
 * Deactivate stream cleanly shuts a stream down, and sends a DISCONNECT message
 * to ingest to bring a stream offline.
 *
 * @param stream_config Active stream configuration
 */

FTL_API ftl_status_t ftl_deactivate_stream(ftl_stream_configuration_t *stream_config);

/*!
 * \ingroup ftl_public
 * \brief Frees a stream configuration structure
 *
 * Releases memory used by a stream structure.
 */

FTL_API void ftl_destory_stream(ftl_stream_configuration_t** stream_config);

/*!
 * \ingroup ftl_public
 * \brief Registers a callback to get libftl messages
 *
 * libftl has internal logging for debugging FTL issues. These messages are normally
 * logged to STDERR, but can be redirected by using this function, and passing a pointer
 * to a function that implements the following prototype:
 *
 * void log_test(ftl_log_severity_t log_level, const char * message)
 *
 * see charon/main.c for an example
 */

FTL_API void ftl_register_log_handler(ftl_logging_function_t log_func);

// Load the internal API if necessary
#ifdef __FTL_INTERNAL
#include "ftl_private.h"
#endif // __FTL_INTERNAL

#endif // __FTL_H
