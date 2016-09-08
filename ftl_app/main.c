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

//-i ingest - sea.beam.pro - s "82585-3s5iskinhxous0czsdmggwq8fd4fyyu5" - v c : \test\sintel.h264 - a c : \test\sintel.opus

 #include "main.h"

//#include <pthread.h>

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#endif

#define MAX_OGG_PAGE_LEN 30000
typedef struct {
	FILE *fp;
	uint8_t *page_buf;
	int page_len;
	int consumed;
	uint8_t version;
	uint8_t header_type;
	uint8_t seg_length;
	uint8_t page_segs;
	uint64_t granule_pos;
	uint32_t bs_serial;
	uint32_t page_sn;
	uint32_t checksum;
	uint8_t seg_len_table[255];
	uint8_t current_segment;
	uint8_t packets_in_page;
}opus_obj_t;

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

BOOL get_nalu(FILE *fp, uint8_t *buf, uint32_t *length);
BOOL get_video_frame(FILE *fp, uint8_t *buf, uint32_t *length);
BOOL init_audio(opus_obj_t *handle, const char *audio_file);
BOOL get_audio_packet(opus_obj_t *handle, uint8_t *buf, uint32_t *length);
BOOL reset_audio(opus_obj_t *handle);

int main(int argc, char** argv) {
   ftl_stream_configuration_t* stream_config = 0;
   ftl_stream_video_component_t* video_component = 0;
   ftl_stream_audio_component_t* audio_component = 0;
   ftl_status_t status_code;


   char* ingest_location = NULL;
   char* video_input = NULL;
   char* audio_input = NULL;
   char* stream_key = NULL;
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

while ((c = getopt(argc, argv, "a:i:v:s:?")) != -1) {
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

	if (video_input != NULL) {
		if ((video_fp = fopen(video_input, "rb")) == NULL) {
			printf("Failed to open video input file %s\n", video_input);
			return -1;
		}

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
	ftl_init();
	ftl_handle_t handle;
	ftl_ingest_params_t params;

	params.log_func = log_test;
	params.stream_key = stream_key;//"82585-3s5iskinhxous0czsdmggwq8fd4fyyu5";
	params.video_codec = FTL_VIDEO_H264;
	params.audio_codec = FTL_AUDIO_OPUS;
	params.ingest_hostname = ingest_location;//"ingest-sea.beam.pro";
	params.status_callback = NULL;
	params.video_frame_rate = 30;
	struct timeval proc_start_tv, proc_end_tv, proc_delta_tv;

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

   float video_send_delay = 0;
   float video_time_step = 1000 / params.video_frame_rate;

   float audio_send_accumulator = video_time_step;
   float audio_time_step = 1000 / audio_pps;
   int audio_pkts_sent;

   gettimeofday(&proc_start_tv, NULL);

   while (!ctrlc_pressed()) {
	   uint8_t nalu_type;
	   int audio_read_len;

	   if (feof(video_fp) || feof(opus_handle.fp)) {
		   printf("Restarting Stream\n");
		   fseek(video_fp, 0, SEEK_SET);
		   reset_audio(&opus_handle);
		   continue;
	   }

	   if (get_video_frame(video_fp, h264_frame, &len) == FALSE) {
		   continue;
	   }

	   ftl_ingest_send_media(&handle, FTL_VIDEO_DATA, h264_frame, len);

	   audio_pkts_sent = 0;
	   while (audio_send_accumulator > audio_time_step) {
		   if (get_audio_packet(&opus_handle, audio_frame, &len) == FALSE) {
			   break;
		   }
		   ftl_ingest_send_media(&handle, FTL_AUDIO_DATA, audio_frame, len);
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

		   if (video_send_delay < 0) {
			   video_send_delay = 0;
		   }
		   else {
			   Sleep((DWORD)video_send_delay);
		   }

		   gettimeofday(&proc_start_tv, NULL);
		   
		   video_send_delay -= (float)((int)video_send_delay);

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

 BOOL get_nalu(FILE *fp, uint8_t *buf, uint32_t *length) {
	 uint32_t sc = 0;
	 uint8_t byte;
	 uint32_t pos = 0;
	 BOOL got_sc = FALSE;

	 while (!feof(fp)) {
		 fread(&byte, 1, 1, fp);

		 if (buf != NULL) {
			 buf[pos] = byte;
		 }

		 pos++;

		 sc = (sc << 8) | byte;

		 if (sc == 1 || ((sc & 0xFFFFFF) == 1)) {

			 pos -= 3;

			 if (sc == 1) {
				 pos -= 1;
			 }

			 got_sc = TRUE;
			 break;
		 }
	 }

	 *length = pos;

	 return got_sc;
 }

 BOOL get_video_frame(FILE *fp, uint8_t *buf, uint32_t *length) {
	 BOOL got_sc = FALSE;
	 uint32_t pos = 0;
	 uint8_t nalu_type = 0;

	 while (get_nalu(fp, buf, length) == TRUE) {
		 if (*length == 0) {
			 continue;
		 }

		 nalu_type = buf[0] & 0x1F;
		 //printf("Got nalu type %d of size %d\n", nalu_type, *length);

		 return TRUE;
	 }

	 return FALSE;
 }

 uint8_t get_8bits(uint8_t **buf, uint32_t *len) {
	 uint8_t val = 0;
	 uint32_t bytes = sizeof(uint8_t);

	 if (*len >= bytes) {
		 *len -= bytes;
	 }

	 val = (*buf)[0];

	 (*buf) += bytes;

	 return val;
 }

 uint16_t get_16bits(uint8_t **buf, uint32_t *len) {
	 uint16_t val;
	 uint32_t bytes = sizeof(uint16_t);

	 if (*len >= bytes) {
		 *len -= bytes;
	 }

	 for (int i = sizeof(uint16_t) - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += bytes;

	 return val;
 }

 uint32_t get_32bits(uint8_t **buf, uint32_t *len) {
	 uint32_t val;
	 uint32_t bytes = sizeof(uint32_t);

	 if (*len >= bytes) {
		 *len -= bytes;
	 }

	 for (int i = bytes - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += bytes;

	 return val;
 }

 uint64_t get_64bits(uint8_t **buf, uint32_t *len) {
	 uint64_t val;
	 uint32_t bytes = sizeof(uint64_t);

	 if (*len >= bytes) {
		 *len -= bytes;
	 }

	 for (int i = bytes - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += bytes;

	 return val;
 }

#if 0
 BOOL get_audio_frame(FILE *fp, uint8_t *buf, uint32_t *length) {
	 BOOL got_sc = FALSE;
	 uint32_t pos = 0;
	 uint8_t nalu_type = 0;
	 uint8_t version, header_type, seg_length, page_segs;
	 uint64_t granule_pos;
	 uint32_t bs_serial, page_sn, checksum;
	 uint8_t tmp[1000], *p;

	 get_ogg_page()

	 p = tmp;

	 while (get_ogg_page(fp, tmp, length) == TRUE) {
		 if (*length == 0) {
			 continue;
		 }

		 version = get_8bits(&p, length);
		 header_type = get_8bits(&p, length);
		 granule_pos = get_64bits(&p, length);
		 bs_serial = get_32bits(&p, length);
		 page_sn = get_32bits(&p, length);
		 checksum = get_32bits(&p, length);
		 page_segs = get_8bits(&p, length);

		 for (int i = 0; i < page_segs; i++) {
			 seg_length =  get_8bits(&p, length);
			 fread(buf, 1, seg_length, fp);
		 }
	 }

	 return FALSE;
 }
#endif

 BOOL get_ogg_page(opus_obj_t *handle) {
	 uint32_t magic_num = 0;
	 uint8_t byte;
	 uint32_t pos = 0;
	 BOOL got_page = FALSE;

	 while (!feof(handle->fp)) {
		 fread(&byte, 1, 1, handle->fp);

		 handle->page_buf[pos] = byte;
		 pos++;

		 if (pos >= MAX_OGG_PAGE_LEN) {
			 printf("Error page size exceeds max\n");
			 exit(-1);
		 }

		 magic_num = (magic_num << 8) | byte;

		 if (magic_num == 0x4F676753) {

			 pos -= 4;

			 if (pos == 0) {
				 continue;
			 }

			 uint8_t *p = handle->page_buf;
			 uint32_t bytes_available = pos;

			 handle->packets_in_page = 0;

			 handle->version = get_8bits(&p, &bytes_available);
			 handle->header_type = get_8bits(&p, &bytes_available);
			 handle->granule_pos = get_64bits(&p, &bytes_available);
			 handle->bs_serial = get_32bits(&p, &bytes_available);
			 handle->page_sn = get_32bits(&p, &bytes_available);
			 handle->checksum = get_32bits(&p, &bytes_available);
			 handle->page_segs = get_8bits(&p, &bytes_available);

			 for (int i = 0; i < handle->page_segs; i++) {
				 handle->seg_len_table[i] = get_8bits(&p, &bytes_available);
				 if (handle->seg_len_table[i] != 255) {
					 handle->packets_in_page++;
				 }
			 }

			 printf("Page %d, pos %ul, page segs %d, packets %d\n", handle->page_sn, handle->granule_pos, handle->page_segs, handle->packets_in_page);

			 handle->consumed = pos - bytes_available;
			 handle->current_segment = 0;

			 got_page = TRUE;
			 break;
		 }
	 }

	 handle->page_len = pos;

	 return got_page;
 }

 BOOL init_audio(opus_obj_t *handle, const char *audio_file) {


	 if (audio_file == NULL) {
		 return FALSE;
	 }

	if ((handle->fp = fopen(audio_file, "rb")) == NULL) {
		return FALSE;
	}
	
	if ((handle->page_buf = malloc(MAX_OGG_PAGE_LEN)) == NULL) {
		return FALSE;
	}

	handle->current_segment = 0;
	handle->consumed = 0;
	handle->page_len = 0;
 }

 BOOL reset_audio(opus_obj_t *handle) {
	 fseek(handle->fp, 0, SEEK_SET);

	 handle->consumed = 0;
	 handle->page_len = 0;

	 return TRUE;
 }

 BOOL get_audio_packet(opus_obj_t *handle, uint8_t *buf, uint32_t *length) {

	*length = 0;

	if (handle->consumed >= handle->page_len) {
		if (get_ogg_page(handle) != TRUE) {
			return FALSE;
		}
	}

	int seg_len;

	do {
		seg_len = handle->seg_len_table[handle->current_segment];
		memcpy(buf, handle->page_buf + handle->consumed, seg_len);
		buf += seg_len;
		*length += seg_len;
		handle->consumed += seg_len;
		handle->current_segment++;
	} while (seg_len == 255);

	return TRUE;
 }



