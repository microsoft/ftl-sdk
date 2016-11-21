#include "ftl.h"
#include "ftl_private.h"
#include <curl/curl.h>
#include <jansson.h>

static int _ingest_lookup_ip(const char *ingest_location, char ***ingest_ip);
static int _ingest_compute_score(ftl_ingest_t *ingest);

static size_t _curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

int _ingest_get_hosts(ftl_stream_configuration_private_t *ftl) {
	CURL *curl_handle;
	CURLcode res;
	struct MemoryStruct chunk;
	char *query_result = NULL;
	size_t i = 0;
	int total_ingest_cnt = 0;
	json_error_t error;
	json_t *ingests = NULL, *ingest_item = NULL;

	curl_handle = curl_easy_init();

	chunk.memory = malloc(1);  /* will be grown as needed by realloc */
	chunk.size = 0;    /* no data at this point */

	curl_easy_setopt(curl_handle, CURLOPT_URL, INGEST_LIST_URI);
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, TRUE); //TODO: fix this, bad to bypass ssl
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _curl_write_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ftlsdk/1.0");

#if LIBCURL_VERSION_NUM >= 0x072400
	// A lot of servers don't yet support ALPN
	curl_easy_setopt(curl_handle, CURLOPT_SSL_ENABLE_ALPN, 0);
#endif

	res = curl_easy_perform(curl_handle);

	if (res != CURLE_OK) {
		printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		goto cleanup;
	}

	if ((ingests = json_loadb(chunk.memory, chunk.size, 0, &error)) == NULL) {
		goto cleanup;
	}
	
	size_t size = json_array_size(ingests);

	for (i = 0; i < size; i++) {
		const char *name, *host;
		ingest_item = json_array_get(ingests, i);
		json_unpack(ingest_item, "{s:s, s:s}", "name", &name, "host", &host);

		int total_ips;
		int ii;
		char **ips;
		if ((total_ips = _ingest_lookup_ip(host, &ips)) <= 0) {
			continue;
		}

		for (ii = 0; ii < total_ips; ii++) {

			ftl_ingest_t *ingest_elmt;

			if ((ingest_elmt = malloc(sizeof(ftl_ingest_t))) == NULL) {
				goto cleanup;
			}

			strcpy_s(ingest_elmt->name, sizeof(ingest_elmt->name), name);
			strcpy_s(ingest_elmt->host, sizeof(ingest_elmt->host), host);
			strcpy_s(ingest_elmt->ip, sizeof(ingest_elmt->ip), ips[ii]);
			ingest_elmt->rtt = 10000;
			ingest_elmt->cpu_load = -1;
			free(ips[ii]);

			ingest_elmt->next = NULL;

			if (ftl->ingest_list == NULL) {
				ftl->ingest_list = ingest_elmt;
			}
			else {
				ftl_ingest_t *tail = ftl->ingest_list;
				while (tail->next != NULL) {
					tail = tail->next;
				}

				tail->next = ingest_elmt;
			}

			total_ingest_cnt++;
		}

		free(ips);
	}

cleanup:
	free(chunk.memory);
	curl_easy_cleanup(curl_handle);
	if (ingests != NULL) {
		json_decref(ingests);
	}

	ftl->ingest_count = total_ingest_cnt;

	return total_ingest_cnt;
}

OS_THREAD_ROUTINE _ingest_get_load(void *data) {

	ftl_ingest_t *ingest = (ftl_ingest_t *)data;
	int ret = 0;
	CURL *curl_handle;
	CURLcode res;
	struct MemoryStruct chunk;
	json_error_t error;
	json_t *load = NULL;
	char ip_port[IPV4_ADDR_ASCII_LEN];
	struct timeval start, stop, delta;

        ingest->rtt = 1000;
        ingest->cpu_load = 100;

	curl_handle = curl_easy_init();

	chunk.memory = malloc(1);  /* will be grown as needed by realloc */
	chunk.size = 0;    /* no data at this point */

	sprintf_s(ip_port, sizeof(ip_port), "%s:%d", ingest->ip, INGEST_LOAD_PORT);

	curl_easy_setopt(curl_handle, CURLOPT_URL, ip_port);
	curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1); //need this for linux otherwise subsecond timeouts dont work
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, 500);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _curl_write_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ftlsdk/1.0");

	//not sure how i can time response time from the GET without including the tcp connect other than to do this 2x
	if( (res = curl_easy_perform(curl_handle)) != CURLE_OK){
		ret = -1;
		printf("Failed to query %s: %s\n", ingest->name, curl_easy_strerror(res));
		goto cleanup;		
	}

	free(chunk.memory);
	chunk.memory = malloc(1);  /* will be grown as needed by realloc */
	chunk.size = 0;    /* no data at this point */

	gettimeofday(&start, NULL);
	res = curl_easy_perform(curl_handle);
	gettimeofday(&stop, NULL);
	timeval_subtract(&delta, &stop, &start);
	int ms = (int)timeval_to_ms(&delta);

	if (res != CURLE_OK) {
		ret = -2;
		printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		goto cleanup;
	}

	if ((load = json_loadb(chunk.memory, chunk.size, 0, &error)) == NULL) {
		ret = -3;
		goto cleanup;
	}

	double cpu_load;
	if (json_unpack(load, "{s:f}", "LoadPercent", &cpu_load) < 0) {
		ret = -4;
		goto cleanup;
	}

        ingest->rtt = ms;
	ingest->cpu_load = (float)cpu_load;

cleanup:
	free(chunk.memory);
	curl_easy_cleanup(curl_handle);

	return (OS_THREAD_TYPE)ret;
}

char * ingest_get_ip(ftl_stream_configuration_private_t *ftl, char *host) {
	if (ftl->ingest_list == NULL) {
		if (_ingest_get_hosts(ftl) <= 0) {
			return NULL;
		}
	}

	ftl_ingest_t * elmt = ftl->ingest_list;

	while (elmt != NULL) {
		if (strcmp(host, elmt->host) == 0) {
			/*just find first in list with matching host, these are on rr dns so first items will be different each time*/
			return elmt->ip;
		}

		elmt = elmt->next;
	}

	return NULL;
}

char * ingest_find_best(ftl_stream_configuration_private_t *ftl) {

	OS_THREAD_HANDLE *handle;
	int i;
	ftl_ingest_t *elmt, *best = NULL;
	struct timeval start, stop, delta;
	float best_ingest_score = 100000, ingest_score;

	if (ftl->ingest_list == NULL) {
		if (_ingest_get_hosts(ftl) <= 0) {
			return NULL;
		}
	}

	if ((handle = (OS_THREAD_HANDLE *)malloc(sizeof(OS_THREAD_HANDLE) * ftl->ingest_count)) == NULL) {
		return NULL;
	}

	gettimeofday(&start, NULL);

	/*query all the ingests about cpu and rtt*/
	elmt = ftl->ingest_list;
	for (i = 0; i < ftl->ingest_count && elmt != NULL; i++) {
		handle[i] = 0;
		//if (strcmp(elmt->name, "EU: Milan") == 0) 
		{
			os_create_thread(&handle[i], NULL, _ingest_get_load, elmt);
			sleep_ms(10); //prevents all the threads from hammering the network at the same time and gives a more reliable rtt
		}
		elmt = elmt->next;
	}

	/*wait for all the ingests to complete*/
	elmt = ftl->ingest_list;
	for (i = 0; i < ftl->ingest_count && elmt != NULL; i++) {
		if (handle[i] != 0) {
			os_wait_thread(handle[i]);
		}

		ingest_score = (float)_ingest_compute_score(elmt);

		if (ingest_score < best_ingest_score ) {
			best_ingest_score = ingest_score;
			best = elmt;
		}

		elmt = elmt->next;
	}

	gettimeofday(&stop, NULL);
	timeval_subtract(&delta, &stop, &start);
	int ms = (int)timeval_to_ms(&delta);

	printf("Took %d ms to query all ingests\n", ms);

	elmt = ftl->ingest_list;
	for (i = 0; i < ftl->ingest_count && elmt != NULL; i++) {
		if (handle[i] != 0) {
			os_destroy_thread(handle[i]);
		}

		elmt = elmt->next;
	}

	if (best){
		FTL_LOG(ftl, FTL_LOG_INFO, "%s at ip %s had the shortest RTT of %d ms with a server load of %2.1f\n", best->name, best->ip, best->rtt, best->cpu_load * 100.f);
		return best->ip;
	}


	return NULL;
}

static int _ingest_compute_score(ftl_ingest_t *ingest) {

	//TODO:  need to weight cpu load less on the low end (under 50%) and more agressively on the high end
	float rtt_percent = (float)ingest->rtt / 200.f;

	if (rtt_percent > 1) {
		rtt_percent = 1;
	}

	int load_score, rtt_score;

	load_score = (int)(ingest->cpu_load * 30);
	rtt_score = (int)(rtt_percent * 70);

	return (int)load_score + rtt_score;
}

static int _ingest_lookup_ip(const char *ingest_location, char ***ingest_ip) {
	struct hostent *remoteHost;
	struct in_addr addr;
	int ips_found = 0;
	BOOL success = FALSE;
	ingest_ip[0] = '\0';

	if (*ingest_ip != NULL) {
		return -1;
	}

	remoteHost = gethostbyname(ingest_location);

	if (remoteHost) {
		if (remoteHost->h_addrtype == AF_INET)
		{
			int total_ips = 0;
			while (remoteHost->h_addr_list[total_ips++] != 0);

			if ((*ingest_ip = malloc(sizeof(char*) * total_ips)) == NULL) {
				return 0;
			}

			while (remoteHost->h_addr_list[ips_found] != 0) {
				addr.s_addr = *(u_long *)remoteHost->h_addr_list[ips_found];

				if (((*ingest_ip)[ips_found] = malloc(IPV4_ADDR_ASCII_LEN)) == NULL) {
					return 0;
				}

				strcpy_s((*ingest_ip)[ips_found], IPV4_ADDR_ASCII_LEN, inet_ntoa(addr));

				ips_found++;
			}
		}
	}

	return ips_found;
}
