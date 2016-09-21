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

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#include "win32/gettimeofday.h"
#endif
#include "file_parser.h"

void log_test(ftl_log_severity_t log_level, const char * message) {
  fprintf(stderr, "libftl message: %s\n", message);
  return;
}

void usage() {
    printf("Usage: ftl_app -i <ingest uri> -s <stream_key> - v <h264_annex_b_file> -a <opus in ogg container>\n");
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

#ifdef _WIN32
DWORD WINAPI ftl_status_thread(LPVOID data);
#else
static void *ftl_status_thread(void *data);
#endif

int main(int argc, char** argv) {
   ftl_stream_configuration_t* stream_config = 0;
   ftl_stream_video_component_t* video_component = 0;
   ftl_stream_audio_component_t* audio_component = 0;
   ftl_status_t status_code;


   char* ingest_location = NULL;
   char* video_input = NULL;
   char* audio_input = NULL;
   char* stream_key = NULL;
   int input_framerate = 30;
   int c;
   int audio_pps = 50;

int success = 0;
int verbose = 0;

opterr = 0;

charon_install_ctrlc_handler();

if (FTL_VERSION_MAINTENANCE != 0) {
	printf("FTLSDK - version %d.%d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR, FTL_VERSION_MAINTENANCE);
}
else {
	printf("FTLSDK - version %d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR);
}

while ((c = getopt(argc, argv, "a:i:v:s:f:?")) != -1) {
	switch (c) {
	case 'i':
		ingest_location = optarg;
		break;
	case 'v':
		video_input = optarg;
		break;
	case 'a':
		audio_input = optarg;
		break;
	case 's':
		stream_key = optarg;
		break;
	case 'f':
		sscanf(optarg, "%d", &input_framerate);
		break;
	case '?':
		usage();
		break;
	}
}

/* Make sure we have all the required bits */
if (!stream_key || !ingest_location || !video_input) {
	usage();
}

#if 0
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
#endif

	FILE *video_fp = NULL;	
	uint32_t len = 0;
	uint8_t *h264_frame;
	uint8_t *audio_frame;
	opus_obj_t opus_handle;
	h264_obj_t h264_handle;

	if (video_input != NULL) {
		if ((h264_frame = malloc(10000000)) == NULL) {
			printf("Failed to allocate memory for bitstream\n");
			return -1;
		}

		if ((audio_frame = malloc(1000)) == NULL) {
			printf("Failed to allocate memory for bitstream\n");
			return -1;
		}

	}
	else {
		return -1;
	}

	init_audio(&opus_handle, audio_input);
	init_video(&h264_handle, video_input);
	ftl_init();
	ftl_handle_t handle;
	ftl_ingest_params_t params;

	params.log_func = log_test;
	params.stream_key = stream_key;
	params.video_codec = FTL_VIDEO_H264;
	params.audio_codec = FTL_AUDIO_OPUS;
	params.ingest_hostname = ingest_location;
	params.status_callback = NULL;
	params.video_frame_rate = (float)input_framerate;
	struct timeval proc_start_tv, proc_end_tv, proc_delta_tv;
	struct timeval profile_start, profile_stop, profile_delta;
	HANDLE status_thread_handle;
	DWORD status_thread_id;

	if( (status_code = ftl_ingest_create(&handle, &params)) != FTL_SUCCESS){
		printf("Failed to create ingest handle %d\n", status_code);
		return -1;
	}

   if ((status_code = ftl_ingest_connect(&handle)) != FTL_SUCCESS) {
	   printf("Failed to connect to ingest %d\n", status_code);
	   return -1;
	}

#if 0
#ifdef _WIN32
   if ((status_thread_handle = CreateThread(NULL, 0, ftl_status_thread, &handle, 0, &status_thread_id)) == NULL) {
#else
   if ((pthread_create(&media->recv_thread, NULL, recv_thread, ftl)) != 0) {
#endif
	   return FTL_MALLOC_FAILURE;
   }
#endif

   printf("Stream online!\n");
   printf("Press Ctrl-C to shutdown your stream in this window\n");

   float video_send_delay = 0, actual_sleep;
   float video_time_step = 1000 / params.video_frame_rate;

   float audio_send_accumulator = video_time_step;
   float audio_time_step = 1000 / audio_pps;
   int audio_pkts_sent;
   int end_of_frame;

   gettimeofday(&proc_start_tv, NULL);

   while (!ctrlc_pressed()) {
	   uint8_t nalu_type;
	   int audio_read_len;

	   if (feof(h264_handle.fp) || feof(opus_handle.fp)) {
		   printf("Restarting Stream\n");
		   reset_video(&h264_handle);
		   reset_audio(&opus_handle);
		   continue;
	   }

	   if (get_video_frame(&h264_handle, h264_frame, &len, &end_of_frame) == FALSE) {
		   continue;
	   }

	   ftl_ingest_send_media(&handle, FTL_VIDEO_DATA, h264_frame, len, end_of_frame);

	   audio_pkts_sent = 0;
	   while (audio_send_accumulator > audio_time_step) {
		   if (get_audio_packet(&opus_handle, audio_frame, &len) == FALSE) {
			   break;
		   }
		   ftl_ingest_send_media(&handle, FTL_AUDIO_DATA, audio_frame, len, 0);
		   audio_send_accumulator -= audio_time_step;
		   audio_pkts_sent++;
	   }
	   	   
	   nalu_type = h264_frame[0] & 0x1F;

	   /*this wont work if there are multiple nalu's per frame...need to pull out frame number from slice header to be more robust*/
	   if (nalu_type == 1 || nalu_type == 5) {
		   gettimeofday(&proc_end_tv, NULL);
		   timeval_subtract(&proc_delta_tv, &proc_end_tv, &proc_start_tv);

		   video_send_delay += video_time_step;

		   video_send_delay -= timeval_to_ms(&proc_delta_tv);

		   if (video_send_delay > 0){
			   gettimeofday(&profile_start, NULL);
			   Sleep((DWORD)video_send_delay);
			   gettimeofday(&profile_stop, NULL);
			   timeval_subtract(&profile_delta, &profile_stop, &profile_start);
			   actual_sleep = timeval_to_ms(&profile_delta);
			   //printf("Requested Sleep %f ms, actual %f ms\n", video_send_delay, actual_sleep);
		   }
		   else {
			   actual_sleep = 0;
		   }

		   video_send_delay -= actual_sleep;

		   gettimeofday(&proc_start_tv, NULL);

		   audio_send_accumulator += video_time_step;
	   }
   }
   
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

#ifdef _WIN32
 DWORD WINAPI ftl_status_thread(LPVOID data)
#else
 static void *ftl_status_thread(void *data)
#endif
 {
	 ftl_handle_t *handle = (ftl_handle_t*)data;
	 ftl_status_msg_t status;

	 while (1) {
		 ftl_ingest_get_status(handle, &status, INFINITE);

		 printf("Status:  Got Status message of type %d\n", status.type);
	 }
 }