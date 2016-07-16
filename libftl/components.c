/**
 * components.c - Code common to all component functions
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

void ftl_attach_audio_component_to_stream(ftl_stream_configuration_t* stream_config,
   ftl_stream_audio_component_t* component) {
     ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
     config->audio_component = component;
}

void ftl_attach_video_component_to_stream(ftl_stream_configuration_t* stream_config,
  ftl_stream_video_component_t* component) {
    ftl_stream_configuration_private_t* config = (ftl_stream_configuration_private_t*)stream_config->private;
    config->video_component = component;
}

ftl_stream_audio_component_t* ftl_create_audio_component(
  ftl_audio_codec_t codec, uint8_t payload_type, uint32_t ssrc) {

//#warning SSRC not dynamically generated yet
//#warning PT not sensible set

  /* Allocate the component bits */
  ftl_stream_audio_component_t* component = malloc(sizeof(ftl_stream_audio_component_t));
  component->private = malloc(sizeof(ftl_stream_audio_component_private_common_t));
  ftl_stream_audio_component_private_common_t *component_internal = component->private;

  /* Copy the struct data in, and return */
  component_internal->codec = codec;
  component_internal->payload_type = payload_type; /* FIXME: Validate the 7bit restriction */
  component_internal->ssrc = ssrc;

  return component;
}

ftl_stream_video_component_t* ftl_create_video_component(
  ftl_video_codec_t codec, uint8_t payload_type, uint32_t ssrc, uint32_t width, uint32_t height) {

//#warning SSRC not dynamically generated yet
//#warning PT not sensible set

  /* Allocate the component bits */
  ftl_stream_video_component_t* component = malloc(sizeof(ftl_stream_video_component_t));
  component->private = malloc(sizeof(ftl_stream_video_component_private_common_t));
  ftl_stream_video_component_private_common_t *component_internal = component->private;

  /* Copy the struct data in, and return */
  component_internal->codec = codec;
  component_internal->payload_type = payload_type; /* FIXME: Validate the 7bit restriction */
  component_internal->ssrc = ssrc;
  component_internal->height = height;
  component_internal->width = width;

  return component;
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
