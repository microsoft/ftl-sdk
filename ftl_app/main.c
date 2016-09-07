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

BOOL get_nalu(FILE *fp, uint8_t *buf, uint32_t *length);
BOOL get_video_frame(FILE *fp, uint8_t *buf, uint32_t *length);
BOOL get_audio_frame(FILE *fp, uint8_t *buf, uint32_t *length);

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
   int audio_bps = 160000;
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
#if 0
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
#endif
	case 'i':
		ingest_location = optarg;
		break;
	case 'v':
		video_input = optarg;
		break;
	case 'a':
		audio_input = optarg;
		break;
#if 0
	case 'w':
		success = sscanf(optarg, "%d", &video_width);
		if (success != 1) {
			printf("ERROR: video width must be numeric");
			return -1;
		}
		break;
#endif
	case 's':
		stream_key = optarg;
		success = sscanf(optarg, "%s", stream_key);
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
	FILE *audio_fp = NULL;
	uint32_t len = 0;
	uint8_t *h264_frame;
	uint8_t *audio_frame;
	uint8_t *dummy;

	if (video_input != NULL) {
		if ((video_fp = fopen(video_input, "rb")) == NULL) {
			printf("Failed to open video input file %s\n", video_input);
			return -1;
		}

		if ((h264_frame = malloc(10000000)) == NULL) {
			printf("Failed to allocate memory for bitstream\n");
			return -1;
		}

		if ((dummy = malloc(500)) == NULL) {
			printf("Failed to allocate memory for bitstream\n");
			return -1;
		}

		memset(dummy, 0, 500);
	}
	else {
		return -1;
	}

	if (audio_input != NULL) {
		if ((audio_fp = fopen(audio_input, "rb")) == NULL) {
			printf("Failed to open video input file %s\n", audio_input);
			return -1;
		}

		if ((audio_frame = malloc(1000)) == NULL) {
			printf("Failed to allocate memory for bitstream\n");
			return -1;
		}
	}

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


   while (!ctrlc_pressed()) {
	   uint8_t nalu_type;
	   int audio_read_len;
	   if (feof(video_fp)) {
		   printf("Restarting Stream\n");
		   fseek(video_fp, 0, SEEK_SET);
		   fseek(audio_fp, 0, SEEK_SET);
		   continue;
	   }

	   if (get_video_frame(video_fp, h264_frame, &len) == FALSE) {
		   continue;
	   }

	   ftl_ingest_send_media(&handle, FTL_VIDEO_DATA, h264_frame, len);

	   if (get_audio_frame(audio_fp, audio_frame, &len) == FALSE) {
		   continue;
	   }

	   audio_read_len = audio_bps / audio_pps / 8;

	   audio_frame[0] = 0xFC;
	   fread(audio_frame + 1, 1, audio_read_len, audio_fp);
	   ftl_ingest_send_media(&handle, FTL_AUDIO_DATA, audio_frame, audio_read_len + 1);

	   audio_frame[0] = 0xFC;
	   fread(audio_frame + 1, 1, audio_read_len, audio_fp);
	   ftl_ingest_send_media(&handle, FTL_AUDIO_DATA, audio_frame, audio_read_len + 1);

	   nalu_type = h264_frame[0] & 0x1F;

	   if (nalu_type == 1 || nalu_type == 5) {
		   Sleep((DWORD)(1000.f / params.video_frame_rate));
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

 BOOL get_ogg_page(FILE *fp, uint8_t *buf, uint32_t *length) {
	 uint32_t magic_num = 0;
	 uint8_t byte;
	 uint32_t pos = 0;
	 BOOL got_page = FALSE;

	 while (!feof(fp)) {
		 fread(&byte, 1, 1, fp);

		 if (buf != NULL) {
			 buf[pos] = byte;
		 }

		 pos++;

		 magic_num = (magic_num << 8) | byte;

		 if (magic_num == 0x4F676753) {

			 pos -= 4;

			 got_page = TRUE;
			 break;
		 }
	 }

	 *length = pos;

	 return got_page;
 }

 uint8_t get_8bits(uint8_t **buf, uint32_t *len) {
	 uint8_t val = 0;

	 val = (*buf)[0];

	 (*buf) += 1;

	 return val;
 }

 uint16_t get_16bits(uint8_t **buf, uint32_t *len) {
	 uint16_t val;

	 for (int i = sizeof(uint16_t) - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += 2;

	 return val;
 }

 uint32_t get_32bits(uint8_t **buf, uint32_t *len) {
	 uint32_t val;

	 for (int i = sizeof(uint32_t) - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += 4;

	 return val;
 }

 uint64_t get_64bits(uint8_t **buf, uint32_t *len) {
	 uint64_t val;

	 for (int i = sizeof(uint64_t) - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += 8;

	 return val;
 }

 BOOL get_audio_frame(FILE *fp, uint8_t *buf, uint32_t *length) {
	 BOOL got_sc = FALSE;
	 uint32_t pos = 0;
	 uint8_t nalu_type = 0;
	 uint8_t version, header_type, seg_length, page_segs;
	 uint64_t granule_pos;
	 uint32_t bs_serial, page_sn, checksum;
	 uint8_t tmp[1000], *p;

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



