#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <Windows.h>
#endif


#include "ftl.h"
#include "file_parser.h"

int h264_get_nalu(FILE *fp, uint8_t *buf, uint32_t *length);
int get_ogg_page(opus_obj_t *handle);

int init_video(h264_obj_t *handle, const char *video_file) {

	if (video_file == NULL) {
		return 0;
	}

	if ((handle->fp = fopen(video_file, "rb")) == NULL) {
		return 0;
	}

	return 1;
}

int reset_video(h264_obj_t *handle) {
	fseek(handle->fp, 0, SEEK_SET);
	
	return 1;
}

int get_video_frame(h264_obj_t *handle, uint8_t *buf, uint32_t *length, int *get_video_frame) {
	 int got_sc = 0;
	 uint32_t pos = 0;
	 uint8_t nalu_type = 0;

	 while (h264_get_nalu(handle->fp, buf, length) == 1) {
		 if (*length == 0) {
			 continue;
		 }

		 nalu_type = buf[0] & 0x1F;
		 *get_video_frame = 0;
		 if (nalu_type == 1 || nalu_type == 5)
		 {
			 *get_video_frame = 1;
		 }

		 return 1;
	 }

	 return 0;
 }

 int init_audio(opus_obj_t *handle, const char *audio_file) {


	 if (audio_file == NULL) {
		 return 0;
	 }

	 if ((handle->fp = fopen(audio_file, "rb")) == NULL) {
		 return 0;
	 }

	 if ((handle->page_buf = malloc(MAX_OGG_PAGE_LEN)) == NULL) {
		 return 0;
	 }

	 handle->current_segment = 0;
	 handle->consumed = 0;
	 handle->page_len = 0;

	 return 1;
 }

 void close_audio(opus_obj_t *handle) {
	 if (handle->page_buf != NULL) {
		 free(handle->page_buf);
	 }
 }

 int reset_audio(opus_obj_t *handle) {
	 fseek(handle->fp, 0, SEEK_SET);

	 handle->consumed = 0;
	 handle->page_len = 0;

	 return 1;
 }
 
 int get_audio_packet(opus_obj_t *handle, uint8_t *buf, uint32_t *length) {

	 *length = 0;

	 if (handle->consumed >= handle->page_len) {
		 if (get_ogg_page(handle) != 1) {
			 return 0;
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

	 return 1;
 }

 int h264_get_nalu(FILE *fp, uint8_t *buf, uint32_t *length) {
	 uint32_t sc = 0;
	 uint8_t byte;
	 uint32_t pos = 0;
	 int got_sc = 0;

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

			 got_sc = 1;
			 break;
		 }
	 }

	 *length = pos;

	 return got_sc;
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

	 int i;
	 for (i = sizeof(uint16_t) - 1; i >= 0; i--) {
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
         
         int i;
	 for (i = bytes - 1; i >= 0; i--) {
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

         int i;
	 for (i = bytes - 1; i >= 0; i--) {
		 val = (val << 8) | (*buf)[i];
	 }

	 (*buf) += bytes;

	 return val;
 }

 int get_ogg_page(opus_obj_t *handle) {
	 uint32_t magic_num = 0;
	 uint8_t byte;
	 uint32_t pos = 0;
	 int got_page = 0;

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

                         int i;
			 for (i = 0; i < handle->page_segs; i++) {
				 handle->seg_len_table[i] = get_8bits(&p, &bytes_available);
				 if (handle->seg_len_table[i] != 255) {
					 handle->packets_in_page++;
				 }
			 }

			 //printf("Page %d, pos %ul, page segs %d, packets %d\n", handle->page_sn, handle->granule_pos, handle->page_segs, handle->packets_in_page);

			 handle->consumed = pos - bytes_available;
			 handle->current_segment = 0;

			 got_page = 1;
			 break;
		 }
	 }

	 handle->page_len = pos;

	 return got_page;
 }



