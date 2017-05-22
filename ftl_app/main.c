

#include "main.h"
#include "gettimeofday.h"
#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC  
#include <stdlib.h>  
#include <crtdbg.h> 
#include <Windows.h>
#include <WinSock2.h>
#else
#include <pthread.h>
#include <sys/time.h>
#endif
#include "file_parser.h"
#include "pcap.h"

void sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

void log_test(ftl_log_severity_t log_level, const char *message)
{
  fprintf(stderr, "libftl message: %s\n", message);
  return;
}

void usage()
{
  printf("Usage: ftl_app -i <ingest uri> -s <stream_key> - v <h264_annex_b_file> -a <opus in ogg container>\n");
  exit(0);
}

#ifdef _WIN32
int WINAPI ftl_status_thread(LPVOID data);
#else
static void *ftl_status_thread(void *data);
#endif

int speedtest_duration = 0;

int main(int argc, char **argv)
{
  ftl_status_t status_code;
  
  char *ingest_location = NULL;
  char *video_input = NULL;
  char *audio_input = NULL;
  char *pcap_input = NULL;
  char *stream_key = NULL;
  int fps_num = 30;
  int fps_den = 1;
  int raw_opus = 0;
  int speedtest_kbps = 1000;
  int c;
  int audio_pps = 50;
  int target_bw_kbps = 0;

  int success = 0;
  int verbose = 0;

  opterr = 0;

  charon_install_ctrlc_handler();

    if (FTL_VERSION_MAINTENANCE != 0)
  {
    printf("FTLSDK - version %d.%d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR, FTL_VERSION_MAINTENANCE);
  }
  else
  {
    printf("FTLSDK - version %d.%d\n", FTL_VERSION_MAJOR, FTL_VERSION_MINOR);
  }

  while ((c = getopt(argc, argv, "a:i:v:p:s:f:b:t:r:?")) != -1)
  {
    switch (c)
    {
    case 'i':
      ingest_location = optarg;
      break;
    case 'v':
      video_input = optarg;
      break;
    case 'a':
      audio_input = optarg;
      break;
	case 'p':
		pcap_input = optarg;
		break;
  case 'r':
    if (strcmp(optarg, "a") == 0) {
      raw_opus = 1;
    }
    break;
    case 's':
      stream_key = optarg;
      break;
    case 'f':
      sscanf(optarg, "%d:%d", &fps_num, &fps_den);
      break;
    case 'b':
      sscanf(optarg, "%d", &target_bw_kbps);
      break;
    case 't':
      sscanf(optarg, "%d:%d", &speedtest_duration, &speedtest_kbps);
      break;
    case '?':
      usage();
      break;
    }
  }

  /* Make sure we have all the required bits */
  if ((!stream_key || !ingest_location) || ((!video_input || !audio_input) && (!speedtest_duration) && !pcap_input))
  {
    usage();
  }

  FILE *video_fp = NULL;
  uint32_t len = 0;
  uint8_t *h264_frame = 0;
  uint8_t *audio_frame = 0;
  opus_obj_t opus_handle;
  h264_obj_t h264_handle;
  pcap_handle_t *pcap_handle;
  int retval = 0;

  if (pcap_input != NULL) 
  {
	  if ( (pcap_handle = pcap_init(pcap_input)) == NULL)
	  {
		  printf("Faild to open pcap file\n");
		  return -1;
	  }
  }

  if (video_input != NULL)
  {
    if ((h264_frame = malloc(10000000)) == NULL)
    {
      printf("Failed to allocate memory for bitstream\n");
      return -1;
    }

    if (!init_video(&h264_handle, video_input))
    {
      printf("Faild to open video file\n");
      return -1;
    }
  }

  if (audio_input != NULL)
  {
    if ((audio_frame = malloc(1000)) == NULL)
    {
      printf("Failed to allocate memory for bitstream\n");
      return -1;
    }

    if (!init_audio(&opus_handle, audio_input, raw_opus))
    {
      printf("Failed to open audio file\n");
      return -1;
    }
  }

  ftl_init();
  ftl_handle_t handle;
  ftl_ingest_params_t params;

  params.stream_key = stream_key;
  params.video_codec = FTL_VIDEO_H264;
  params.audio_codec = FTL_AUDIO_OPUS;
  params.ingest_hostname = ingest_location;
  params.fps_num = fps_num;
  params.fps_den = fps_den;
  params.peak_kbps = target_bw_kbps;
  params.vendor_name = "ftl_app";
  params.vendor_version = "0.0.1";

  struct timeval proc_start_tv, proc_end_tv, proc_delta_tv;
  struct timeval profile_start, profile_stop, profile_delta;
#ifdef _WIN32
  HANDLE status_thread_handle;
  int status_thread_id;
#else
  pthread_t status_thread_handle;
#endif

  if ((status_code = ftl_ingest_create(&handle, &params)) != FTL_SUCCESS)
  {
    printf("Failed to create ingest handle: %s\n", ftl_status_code_to_string(status_code));
    return -1;
  }

  if ((status_code = ftl_ingest_connect(&handle)) != FTL_SUCCESS)
  {
    printf("Failed to connect to ingest: %s\n", ftl_status_code_to_string(status_code));
    return -1;
  }
#ifdef _WIN32
  if ((status_thread_handle = CreateThread(NULL, 0, ftl_status_thread, &handle, 0, &status_thread_id)) == NULL)
  {
#else
  if ((pthread_create(&status_thread_handle, NULL, ftl_status_thread, &handle)) != 0)
  {
#endif
    return FTL_MALLOC_FAILURE;
  }

  if (speedtest_duration)
  {
    printf("Running Speed test: sending %d kbps for %d ms", speedtest_kbps, speedtest_duration);
  speed_test_t results;
  if ((status_code = ftl_ingest_speed_test_ex(&handle, speedtest_kbps, speedtest_duration, &results)) == FTL_SUCCESS) {
    printf("Speed test completed: Peak kbps %d, initial rtt %d, final rtt %d, %3.2f lost packets\n", 
      results.peak_kbps, results.starting_rtt, results.ending_rtt, (float)results.lost_pkts * 100.f / (float)results.pkts_sent);
  }
  else {
    printf("Speed test failed with: %s\n", ftl_status_code_to_string(status_code));
  }

    goto cleanup;
  }

  printf("Stream online!\n");
  printf("Press Ctrl-C to shutdown your stream in this window\n");

  float video_send_delay = 0, actual_sleep, time_delta;
  float video_time_step = (float)(1000 * params.fps_den) / (float)params.fps_num;

  float audio_send_accumulator = video_time_step;
  float audio_time_step = 1000.f / audio_pps;
  int audio_pkts_sent;
  int end_of_frame;
  int got_first_pkt = 0;
  struct timeval prev_ts, delta_ts;
  int video_last_sn = -1, audio_last_sn = -1;
  int video_last_ts, audio_last_ts;
  int video_sn = 0, audio_sn = 0;
  int video_ts = 0, audio_ts = 0;
  float sleep_accumulator_us = 0;
  int got_keyframe = 0;
  
  gettimeofday(&proc_start_tv, NULL);
  struct timeval frameTime;
  gettimeofday(&frameTime, NULL); // NOTE! In a real app these timestamps should come from the samples!

  while (!ctrlc_pressed())
  {	
	  if (pcap_input) {
		  pcap_pkt_t *pkt = NULL;

		  if ((pkt = pcap_read_packet(pcap_handle)) != NULL) 
		  {
			  if (pkt->rtp_header.payload_type == 96 || pkt->rtp_header.payload_type == 97) 
			  {
				  if (!got_keyframe) 
				  {
					  if (pkt->rtp_header.payload_type == 96)
					  {
						  uint8_t nalu_type = (pkt->udp_payload[12] & 0x1F);

						  if (nalu_type == 7) {
							  got_keyframe = 1;
						  }
						  else if (nalu_type == 24) {
							  //skip 2 bytes for packet size
							  if ((pkt->udp_payload[15] & 0x1F) == 7) {
								  got_keyframe = 1;
							  }
						  }
					  }
				  }

				  if (!got_keyframe) {
					  continue;
				  }

				  if (pkt->rtp_header.payload_type == 96 && video_last_sn >= 0) {
					  video_sn += pkt->rtp_header.sequence_number - video_last_sn;
					  video_ts += pkt->rtp_header.timestamp - video_last_ts;
				  }
				  else if (pkt->rtp_header.payload_type == 97 && audio_last_sn >= 0) {
					  audio_sn += pkt->rtp_header.sequence_number - audio_last_sn;
					  audio_ts += pkt->rtp_header.timestamp - audio_last_ts;
				  }

				  if (got_first_pkt) {
					  float ms_delta;

					  timeval_subtract(&delta_ts, &pkt->pkt_header.ts, &prev_ts);
					  sleep_accumulator_us += timeval_to_us(&delta_ts);

					  if (sleep_accumulator_us >= 1000000) {
						  gettimeofday(&profile_start, NULL);
						  sleep_ms(sleep_accumulator_us / 1000000);
						  gettimeofday(&profile_stop, NULL);
						  timeval_subtract(&profile_delta, &profile_stop, &profile_start);
						  sleep_accumulator_us -= timeval_to_us(&profile_delta);
					  }
				  }

				  got_first_pkt = 1;

				  uint16_t *sn = pkt->udp_payload + 2;
				  uint32_t *ts = pkt->udp_payload + 4;

				  if (pkt->rtp_header.payload_type == 96) {
					  *sn = htons(video_sn);
					  *ts = htonl(video_ts);
				  }
				  else if (pkt->rtp_header.payload_type == 97) {
					  *sn = htons(audio_sn);
					  *ts = htonl(audio_ts);
				  }

				  ftl_ingest_send_media_raw(&handle, pkt->udp_payload, pkt->udp_payload_len);

				  prev_ts = pkt->pkt_header.ts;
				  
				  if (pkt->rtp_header.payload_type == 96) {
					  video_last_sn = pkt->rtp_header.sequence_number;
					  video_last_ts = pkt->rtp_header.timestamp;
				  }
				  else if (pkt->rtp_header.payload_type == 97) {
					  audio_last_sn = pkt->rtp_header.sequence_number;
					  audio_last_ts = pkt->rtp_header.timestamp;
				  }
			  }

			  free(pkt);
		  }
		  else if(pcap_eof(pcap_handle))
		  {
			  got_first_pkt = 0;
			  pcap_reset(pcap_handle);
			  video_sn++;
			  video_ts += 90000 / 30;
			  audio_sn++;
			  audio_ts += 48000 / 50;
			  video_last_sn = -1;
			  audio_last_sn = -1;
			  got_keyframe = 0;
			  printf("Restarting Stream\n");
		  }
	  }
	  else {

		  uint8_t nalu_type;
		  int audio_read_len;

		  if (feof(h264_handle.fp) || feof(opus_handle.fp))
		  {
			  printf("Restarting Stream\n");
			  reset_video(&h264_handle);
			  reset_audio(&opus_handle);
			  continue;
		  }

		  if (get_video_frame(&h264_handle, h264_frame, &len, &end_of_frame) == 0)
		  {
			  continue;
		  }

		  ftl_ingest_send_media_dts(&handle, FTL_VIDEO_DATA, timeval_to_us(&frameTime), h264_frame, len, end_of_frame);

		  audio_pkts_sent = 0;
		  while (audio_send_accumulator > audio_time_step)
		  {
			  if (get_audio_packet(&opus_handle, audio_frame, &len) == 0)
			  {
				  break;
			  }

			  ftl_ingest_send_media(&handle, FTL_AUDIO_DATA, audio_frame, len, 0);
			  audio_send_accumulator -= audio_time_step;
			  audio_pkts_sent++;
		  }

		  nalu_type = h264_frame[0] & 0x1F;

		  if (end_of_frame)
		  {
			  gettimeofday(&frameTime, NULL); // NOTE! In a real app these timestamps should come from the samples!
			  gettimeofday(&proc_end_tv, NULL);
			  timeval_subtract(&proc_delta_tv, &proc_end_tv, &proc_start_tv);

			  video_send_delay += video_time_step;
			  time_delta = (float)timeval_to_ms(&proc_delta_tv);
			  video_send_delay -= time_delta;

			  if (video_send_delay > 0)
			  {
				  gettimeofday(&profile_start, NULL);
				  sleep_ms((int)video_send_delay);
				  gettimeofday(&profile_stop, NULL);
				  timeval_subtract(&profile_delta, &profile_stop, &profile_start);
				  actual_sleep = (float)timeval_to_ms(&profile_delta);
			  }
			  else
			  {
				  actual_sleep = 0;
			  }

			  video_send_delay -= actual_sleep;

			  gettimeofday(&proc_start_tv, NULL);

			  audio_send_accumulator += video_time_step;
		  }
	  }
  }

cleanup:

  if (h264_frame != NULL) {
    free(h264_frame);
  }

  if (audio_frame != NULL) {
    free(audio_frame);
  }

  close_audio(&opus_handle);

  if ((status_code = ftl_ingest_disconnect(&handle)) != FTL_SUCCESS)
  {
    printf("Failed to disconnect from ingest: %s\n", ftl_status_code_to_string(status_code));
    retval = -1;
  }

  if ((status_code = ftl_ingest_destroy(&handle)) != FTL_SUCCESS)
  {
    printf("Failed to disconnect from ingest: %s\n", ftl_status_code_to_string(status_code));
    retval = -1;
  }

#ifdef _WIN32
  WaitForSingleObject(status_thread_handle, INFINITE);
  CloseHandle(status_thread_handle);
#else
  pthread_join(status_thread_handle, NULL);
#endif

#ifdef _WIN32
  _CrtDumpMemoryLeaks();
#endif

  return retval;
}

#ifdef _WIN32
int WINAPI ftl_status_thread(LPVOID data)
#else
static void *ftl_status_thread(void *data)
#endif
{
  ftl_handle_t *handle = (ftl_handle_t *)data;
  ftl_status_msg_t status;
  ftl_status_t status_code;
  int retries = 10;
  int retry_sleep = 5000;

  while (1)
  {
    if ((status_code = ftl_ingest_get_status(handle, &status, 1000)) == FTL_STATUS_TIMEOUT)
    {
      continue;
    }

    if (status_code == FTL_NOT_INITIALIZED)
    {
      break;
    }

  if (status.type == FTL_STATUS_EVENT && status.msg.event.type == FTL_STATUS_EVENT_TYPE_DESTROYED)
  {
    break;
  }

    if (status.type == FTL_STATUS_EVENT && status.msg.event.type == FTL_STATUS_EVENT_TYPE_DISCONNECTED)
    {
      printf("Disconnected from ingest for reason: %s (%d)\n", ftl_status_code_to_string(status.msg.event.error_code), status.msg.event.reason);

      if (status.msg.event.reason == FTL_STATUS_EVENT_REASON_API_REQUEST)
      {
        continue;
      }

    /*dont reconnect for speed test*/
    if (speedtest_duration) {
      continue;
    }

      //attempt reconnection
      while (retries-- > 0)
      {
        sleep_ms(retry_sleep);
        printf("Attempting to reconnect to ingest (retires left %d)\n", retries);
        if ((status_code = ftl_ingest_connect(handle)) == FTL_SUCCESS)
        {
          retries = 10;
          break;
        }
        printf("Failed to connect to ingest: %s (%d)\n", ftl_status_code_to_string(status_code), status_code);
        retry_sleep = retry_sleep * 3 / 2;
      }
      if (retries <= 0)
      {
        break;
      }
    }
    else if (status.type == FTL_STATUS_LOG)
    {
      printf("[%d] %s\n", status.msg.log.log_level, status.msg.log.string);
    }
    else if (status.type == FTL_STATUS_VIDEO_PACKETS)
    {
      ftl_packet_stats_msg_t *p = &status.msg.pkt_stats;

      printf("Avg packet send per second %3.1f, total nack requests %d\n",
             (float)p->sent * 1000.f / p->period,
             p->nack_reqs);
    }
  else if (status.type == FTL_STATUS_VIDEO_PACKETS_INSTANT)
  {
    ftl_packet_stats_instant_msg_t *p = &status.msg.ipkt_stats;

    printf("avg transmit delay %dms (min: %d, max: %d), avg rtt %dms (min: %d, max: %d)\n",
      p->avg_xmit_delay, p->min_xmit_delay, p->max_xmit_delay,
      p->avg_rtt, p->min_rtt, p->max_rtt);
  }
    else if (status.type == FTL_STATUS_VIDEO)
    {
      ftl_video_frame_stats_msg_t *v = &status.msg.video_stats;

      printf("Queue an average of %3.2f fps (%3.1f kbps), sent an average of %3.2f fps (%3.1f kbps), queue fullness %d, max frame size %d\n",
             (float)v->frames_queued * 1000.f / v->period,
             (float)v->bytes_queued / v->period * 8,
             (float)v->frames_sent * 1000.f / v->period,
             (float)v->bytes_sent / v->period * 8,
             v->queue_fullness, v->max_frame_size);
    }
    else
    {
      printf("Status:  Got Status message of type %d\n", status.type);
    }
  }

  printf("exited ftl_status_thread\n");

  return 0;
}
