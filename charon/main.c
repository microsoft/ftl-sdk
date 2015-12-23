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

 #include "charon.h"

void usage() {
    printf("Usage: charon -h host -c channel_id -a authkey\n\n");
    printf("Charon is used to signal to ingest that a FTL stream is online\n");
    printf("\t-i\t\t\tIngest hostname\n");
    printf("\t-c\t\t\tChannel ID\n");
    printf("\t-a\t\t\tAuthetication key for given channel id\n");
    printf("\t-h\t\t\tVideo Height\n");
    printf("\t-w\t\t\tVideo Width\n");
    printf("\t-v\t\t\tVerbose mode\n");
    printf("\t-?\t\t\tThis help message\n");
    exit (0);
}

int main(int argc, char** argv) {
   ftl_stream_configuration_t* stream_config = 0;
   ftl_stream_video_component_t* video_component = 0;
   ftl_stream_audio_component_t* audio_component = 0;
   ftl_status_t status_code;

   int channel_id = 0;
   char* ingest_location = 0;
   char* authetication_key = 0;
   int c;
   int video_height = 0;
   int video_width = 0;
   int success = 0;
   int verbose = 0;

   opterr = 0;

   charon_install_ctrlc_handler();

   if (FTL_VERSION_MAINTENANCE != 0) {
       printf("charon - version %d.%d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR, FTL_VERSION_MAINTENANCE);
   } else {
       printf("charon - version %d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR);
   }

   while ((c = getopt (argc, argv, "a:c:h:i:vw:?")) != -1) {
       switch (c) {
           case 'a':
                authetication_key = optarg;
                break;
            case 'c':
                success = sscanf(optarg, "%d", &channel_id);
                if (success != 1) {
                    printf("ERROR: channel ID must be numeric");
                    return -1;
                }
                break;
            case 'h':
                success = sscanf(optarg, "%d", &video_height);
                if (success != 1) {
                    printf("ERROR: video height must be numeric");
                    return -1;
                }
                break;
            case 'i':
                ingest_location = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'w':
                success = sscanf(optarg, "%d", &video_width);
                if (success != 1) {
                    printf("ERROR: video width must be numeric");
                    return -1;
                }
                break;
            case '?':
                usage();
                break;
       }
   }

   /* Make sure we have all the required bits */
   if (!authetication_key || !ingest_location || !channel_id) {
       usage();
   }

   if (verbose) {
       printf("\nConfiguration:\n");
       printf("\tvideo height: %d\n", video_height);
       printf("\tvideo width: %d\n", video_width);
       printf("\tingesting to: %s\n", ingest_location);
       printf("\tchannel id: %d\n", channel_id);
       printf("\tauthetication key: %s\n", authetication_key);
   }

   ftl_init();
   status_code = ftl_create_stream_configuration(&stream_config);
   if (status_code != FTL_SUCCESS) {
     printf("Failed to initialize stream configuration: errno %d\n", status_code);
     return -1;
   }

   ftl_set_ingest_location(stream_config, ingest_location);
   ftl_set_authetication_key(stream_config, channel_id, authetication_key);

   video_component = ftl_create_video_component(FTL_VIDEO_VP8, 96, 1, video_width, video_height);
   ftl_attach_video_component_to_stream(stream_config, video_component);

   audio_component = ftl_create_audio_component(FTL_AUDIO_OPUS, 97, 2);
   ftl_attach_audio_component_to_stream(stream_config, audio_component);

   if (ftl_activate_stream(stream_config)  != FTL_SUCCESS) {
     printf("Failed to activate stream, see above for error message\n");
     return -1;
   }

   printf("Stream online!\nYou may now start streaming in OBS+gstreamer\n");
   printf("Press Ctrl-C to shutdown your stream in this window\n");

  // Wait until we're ctrl-c'ed
   charon_loop_until_ctrlc();

   printf("\nShutting down\n");
   ftl_deactivate_stream(stream_config);
   ftl_destory_stream(&stream_config);

   return 0;
 }
