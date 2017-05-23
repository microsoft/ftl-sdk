#ifndef _PCAP_H_
#define _PCAP_H_

#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <Winsock2.h>
#endif

typedef struct _pcap_file_header_t {
	unsigned int magic;
	unsigned short version_major;
	unsigned short version_minor;
	int  thiszone;	/* gmt to local correction */
	unsigned int sigfigs;	/* accuracy of timestamps */
	unsigned int snaplen;	/* max length saved portion of each pkt */
	unsigned int linktype;	/* data link type (LINKTYPE_*) */
}pcap_file_header_t;

typedef struct _pcap_timeval_t {
	unsigned int tv_sec;		/* seconds */
	unsigned int tv_usec;		/* microseconds */
}pcap_timeval_t;

typedef struct _pcap_pkt_header_t
{
	struct timeval ts;
	unsigned int caplen;	/* length of portion present */
	unsigned int len;	/* length this packet (off wire) */
}pcap_pkt_header_t;

typedef struct _pcap_eth_header_t {
	unsigned char dst_mac[6];
	unsigned char src_mac[6];
	unsigned short type;
}pcap_eth_header_t;

typedef struct _pcap_ip_header_t {
	unsigned char version;
	unsigned char header_length;
	unsigned char type_of_service;
	unsigned short total_length;
	unsigned short indentification;
	unsigned char  flags;
	unsigned short fragment_offset;
	unsigned char ttl;
	unsigned int  protocol;
	unsigned short header_checksum;
	unsigned int src_ip;
	unsigned int dst_ip;
}pcap_ip_header_t;

typedef struct _pcap_ipv6_header_t {
	unsigned char version;
	unsigned char traffic_class;
	unsigned int flow_label;
	unsigned short payload_len;
	unsigned char next_header;
	unsigned char  hop_limit;
	unsigned int src_addr[4];
	unsigned int dst_addr[4];
}pcap_ipv6_header_t;

typedef struct _pcap_udp_header_t {
	unsigned short src_port;
	unsigned short dst_port;
	unsigned short length;
	unsigned short checksum;
}pcap_udp_header_t;

typedef struct _rtp_fixed_header_t {
	unsigned char version;
	unsigned char padding;
	unsigned char extension;
	unsigned char csrc_count;
	unsigned char marker;
	unsigned char payload_type;
	unsigned short sequence_number;
	unsigned int timestamp;
	unsigned int ssrc;
	unsigned int csrc[16];
}rtp_fixed_header_t;

typedef struct _pcap_pkt_t {
	pcap_pkt_header_t pkt_header;
	pcap_eth_header_t eth_header;
	pcap_ip_header_t ip_header;
	pcap_udp_header_t udp_header;
	rtp_fixed_header_t rtp_header;
	uint8_t udp_payload[1500];
	int udp_payload_len;
}pcap_pkt_t;

typedef struct _pcap_handle_t {
	FILE *fp;
	pcap_file_header_t pcap_file_header;
}pcap_handle_t;

pcap_handle_t *pcap_init(const char *pcap_file);
pcap_pkt_t* pcap_read_packet(pcap_handle_t *handle);
void pcap_reset(pcap_handle_t *handle);
int pcap_eof(pcap_handle_t *handle);

#endif