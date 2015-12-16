/**
 * main.c - Charon client for the FTL SDK
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

 int main(int argc, char** argv) {
   ftl_stream_configuration_t* stream_config = 0;
   ftl_stream_video_component_t* video_component = 0;
   ftl_stream_audio_component_t* audio_component = 0;
   ftl_status_t status_code;

   ftl_init();
   status_code = ftl_create_stream_configuration(&stream_config);
   if (status_code != FTL_SUCCESS) {
     printf("Failed to initialize stream configuration: errno %d\n", status_code);
     return -1;
   }

   ftl_set_ingest_location(stream_config, "localhost");
   ftl_set_authetication_key(stream_config, 1, "testtest");

   video_component = ftl_create_video_component(FTL_VIDEO_VP8, 96, 1, 1280, 720);
   ftl_attach_video_component_to_stream(stream_config, video_component);

   audio_component = ftl_create_audio_component(FTL_AUDIO_OPUS, 97, 2);
   ftl_attach_audio_component_to_stream(stream_config, audio_component);

   ftl_activate_stream(stream_config);
   ftl_destory_stream(&stream_config);
   return 0;
 }
