/**
 * create_stream.c - Stream Creation Functions
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

// Allocates the stream configuration structure
ftl_status_t ftl_create_stream_configuration(ftl_stream_configuration_t** stream_config) {
  /* First step, make sure we got a zero pointer coming in */
  if (*stream_config != 0) {
    return FTL_NON_ZERO_POINTER;
  }

  *stream_config = malloc(sizeof(ftl_stream_configuration_t));
  /* eeek, malloc() should almost never fail in real world conditions */
  if (*stream_config == 0) {
    return FTL_MALLOC_FAILURE;
  }

  /* Allocate the private struct now */
  (*stream_config)->private = malloc(sizeof(ftl_stream_configuration_private_t));
  if ((*stream_config)->private == 0) {
    free (*stream_config);
    return FTL_MALLOC_FAILURE;
  }

  /* Now we need to setup the private members */
  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)(*stream_config)->private;
  config->audio_component = 0;
  config->video_component = 0;
  config->ingest_location = 0;
  config->authetication_key = 0;

  /* and we're done here */
  return FTL_SUCCESS;
}

// Setter for ingest location
void ftl_set_ingest_location(ftl_stream_configuration_t *stream_config, const char * ingest_location) {
  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
  size_t len = 0;

  /* Let's pray we got a valid string */
  len = strlen(ingest_location)+1;
  config->ingest_location = malloc((len*sizeof(char)));
  strcpy(config->ingest_location, ingest_location);
}

// Setter for auth key
void ftl_set_authetication_key(ftl_stream_configuration_t *stream_config, uint32_t channel_id, const char * auth_key) {
  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
  size_t len = 0;

  /* Easy stuff first */
  config->channel_id = channel_id;

  /* Let's pray we got a valid string */
  len = strlen(auth_key)+1;
  config->authetication_key = malloc((len*sizeof(char)));
  strcpy(config->authetication_key, auth_key);
}

// Safely frees all FTL member
void ftl_destory_stream(ftl_stream_configuration_t** stream_config) {
  if (*stream_config == 0) {
    // Ok, someone passed us a zeroed struct. Just return
    return;
  }

  ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)(*stream_config)->private;
  if (config->ingest_location) free(config->ingest_location);
  if (config->authetication_key) free(config->authetication_key);

  // Free the audio component if present
  ftl_stream_audio_component_t* audio_component = config->audio_component;
  if (audio_component) {
    if (audio_component->private) {
      free(audio_component->private);
    }

    free(audio_component);
  }

  // Free the audio component if present
  ftl_stream_video_component_t* video_component = config->video_component;
  if (video_component) {
    if (video_component->private) {
      free(video_component->private);
    }

    free(video_component);
  }

  // Finally, free the private structure if its allocated (should always be)
  if ((*stream_config)->private != 0) {
    free((*stream_config)->private);
  }

  free(*stream_config);
}
