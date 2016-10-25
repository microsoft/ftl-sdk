#include "ftl.h"
#include "ftl_private.h"
#include <curl/curl.h>
#include <jansson.h>

static int _ingest_lookup_ip(const char *ingest_location, char *ingest_ip);

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
	char etcd_uri[100];
	struct MemoryStruct chunk;
	char *query_result = NULL;
	char *etcd_host;
	size_t i = 0;
	int total_ingest_cnt = 0;
	json_error_t error;
	json_t *ingests = NULL, *ingest_item = NULL;

	curl_handle = curl_easy_init();

	chunk.memory = malloc(1);  /* will be grown as needed by realloc */
	chunk.size = 0;    /* no data at this point */

	curl_easy_setopt(curl_handle, CURLOPT_URL, INGEST_LIST_URI);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _curl_write_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ftlsdk/1.0");

	res = curl_easy_perform(curl_handle);

	if (res != CURLE_OK) {
		printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		goto cleanup;
	}

	if ((ingests = json_loads(chunk.memory, chunk.size, 0, &error)) == NULL) {
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

	return total_ingest_cnt;
}

json_t *_ingest_get_load(ftl_ingest_t *ingest) {
	CURL *curl_handle;
	CURLcode res;
	struct MemoryStruct chunk;
	json_error_t error;
	json_t *load = NULL;
	char ip_port[IPV4_ADDR_ASCII_LEN];
	struct timeval start, stop, delta;

	curl_handle = curl_easy_init();

	chunk.memory = malloc(1);  /* will be grown as needed by realloc */
	chunk.size = 0;    /* no data at this point */

	sprintf_s(ip_port, sizeof(ip_port), "%s:%d", ingest->ip, INGEST_LOAD_PORT);

	curl_easy_setopt(curl_handle, CURLOPT_URL, ip_port);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _curl_write_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ftlsdk/1.0");

	gettimeofday(&start, NULL);
	res = curl_easy_perform(curl_handle);
	gettimeofday(&stop, NULL);
	timeval_subtract(&delta, &stop, &start);
	int ms = (int)timeval_to_ms(&delta);
	ingest->rtt = ms;

	if (res != CURLE_OK) {
		printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		goto cleanup;
	}

	if ((load = json_loads(chunk.memory, chunk.size, 0, &error)) == NULL) {
		goto cleanup;
	}

	double cpu_load;
	if (json_unpack(load, "{s:f}", "CPULoadAvg", &cpu_load) < 0) {
		ingest->cpu_load = -1;
	}
	else {
		ingest->cpu_load = cpu_load;
	}

cleanup:
	free(chunk.memory);
	curl_easy_cleanup(curl_handle);

	return load;
}

char * _ingest_find_best(ftl_stream_configuration_private_t *ftl) {

	ftl_ingest_t tmp;

	strcpy(tmp.ip, "169.44.63.80");

	_ingest_get_load(&tmp);

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