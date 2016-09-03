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

 #include "main.h"

void log_test(ftl_log_severity_t log_level, const char * message) {
  fprintf(stderr, "libftl message: %s\n", message);
  return;
}

void usage() {
    printf("Usage: charon -h host -c channel_id -a authkey\n\n");
    printf("Charon is used to signal to ingest that a FTL stream is online\n");
    printf("\t-A\t\t\tset audio SSRC\n");
    printf("\t-V\t\t\tset video SSRC\n");
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
int audio_ssrc = 0;
int video_ssrc = 0;

int success = 0;
int verbose = 0;

opterr = 0;

charon_install_ctrlc_handler();

if (FTL_VERSION_MAINTENANCE != 0) {
	printf("charon - version %d.%d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR, FTL_VERSION_MAINTENANCE);
}
else {
	printf("charon - version %d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR);
}

while ((c = getopt(argc, argv, "A:V:a:c:h:i:vw:?")) != -1) {
	switch (c) {
	case 'A':
		success = sscanf(optarg, "%d", &audio_ssrc);
		if (success != 1) {
			printf("ERROR: Audio SSRC must be numeric");
			return -1;
		}
		break;
	case 'V':
		success = sscanf(optarg, "%d", &video_ssrc);
		if (success != 1) {
			printf("ERROR: Video SSRC must be numeric");
			return -1;
		}
		break;
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

#if 0
/* Make sure we have all the required bits */
if (!authetication_key || !ingest_location || !channel_id || !audio_ssrc || !video_ssrc) {
	usage();
}
#endif

if (verbose) {
	printf("\nConfiguration:\n");
	printf("\taudio ssrc: %d\n", audio_ssrc);
	printf("\tvideo ssrc: %d\n", video_ssrc);
	printf("\tvideo height: %d\n", video_height);
	printf("\tvideo width: %d\n", video_width);
	printf("\tingesting to: %s\n", ingest_location);
	printf("\tchannel id: %d\n", channel_id);
	printf("\tauthetication key: %s\n", authetication_key);
}

	ftl_init();
	ftl_handle_t handle;
	ftl_ingest_params_t params;

	params.log_func = log_test;
	params.stream_key = "82585-3s5iskinhxous0czsdmggwq8fd4fyyu5";
	params.video_codec = FTL_VIDEO_H264;
	params.audio_codec = FTL_AUDIO_OPUS;
	params.ingest_hostname = "ingest-sea.beam.pro";
	params.status_callback = NULL;

	if( (status_code = ftl_ingest_create(&handle, &params)) != FTL_SUCCESS){
		printf("Failed to create ingest handle %d\n", status_code);
		return -1;
	}

   if ((status_code = ftl_ingest_connect(&handle)) != FTL_SUCCESS) {
	   printf("Failed to connect to ingest %d\n", status_code);
	   return -1;
	}

   printf("Stream online!\nYou may now start streaming in OBS+gstreamer\n");
   printf("Press Ctrl-C to shutdown your stream in this window\n");

  // Wait until we're ctrl-c'ed
//   charon_loop_until_ctrlc();

   Sleep(2);

	if ((status_code = ftl_ingest_disconnect(&handle)) != FTL_SUCCESS) {
		printf("Failed to disconnect from ingest %d\n", status_code);
		return -1;
	}

   if ((status_code = ftl_ingest_destroy(&handle)) != FTL_SUCCESS) {
	   printf("Failed to disconnect from ingest %d\n", status_code);
	   return -1;
   }

   return 0;
 }
