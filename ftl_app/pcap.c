#include <stdlib.h>
#include "bitstream.h"
#include "pcap.h"

static int _read_ethernet_header(uint8_t *buf, int buf_len, pcap_eth_header_t *eth);
static int _read_ip_header(uint8_t *buf, int buf_len, pcap_ip_header_t *ip);
static int _read_udp_header(uint8_t *buf, int buf_len, pcap_udp_header_t *udp);
static int _read_rtp_header(uint8_t *buf, int buf_len, rtp_fixed_header_t *rtp);
static int _read_pkt_header(pcap_pkt_header_t *pkt_hdr, FILE *fp);

pcap_handle_t *pcap_init(const char *pcap_file) {
	pcap_handle_t *handle = NULL;

	if ((handle = (pcap_handle_t *)malloc(sizeof(pcap_handle_t))) == NULL) {
		return NULL;
	}

	if ((handle->fp = fopen(pcap_file, "rb")) == NULL) {
		printf("failed to open %s\n", pcap_file);
		return NULL;
	}

	pcap_reset(handle);

	if (handle->pcap_file_header.magic != 0xA1B2C3D4) {
		free(handle);
		printf("failed file magic number in pcap\n");
		return NULL;
	}


	return handle;
}

void pcap_reset(pcap_handle_t *handle) {

	fseek(handle->fp, 0L, SEEK_SET);
	fread(&handle->pcap_file_header, 1, sizeof(pcap_file_header_t), handle->fp);
}

int pcap_eof(pcap_handle_t *handle) {
	return feof(handle->fp);
}

pcap_pkt_t* pcap_read_packet(pcap_handle_t *handle) {
	pcap_pkt_t *pkt;
	uint8_t buf[1500];

	if ((pkt = malloc(sizeof(pcap_pkt_t))) == NULL) {
		return NULL;
	}

	//fread(&pkt->pkt_header, 1, sizeof(pcap_pkt_header_t), handle->fp);
	_read_pkt_header(&pkt->pkt_header, handle->fp);

	if (feof(handle->fp)) {
		free(pkt);
		return NULL;
	}

	fread(buf, 1, pkt->pkt_header.caplen, handle->fp);

	int bytes_read, bytes_left;
	uint8_t *p_buf = buf;
	bytes_left = pkt->pkt_header.caplen;

	bytes_read = _read_ethernet_header(p_buf, bytes_left, &pkt->eth_header);
	p_buf += bytes_read;
	bytes_left -= bytes_read;

	bytes_read = _read_ip_header(p_buf, bytes_left, &pkt->ip_header);
	p_buf += bytes_read;
	bytes_left -= bytes_read;

	if (pkt->ip_header.protocol != 17) {
		free(pkt);
		return NULL;
	}

	bytes_read = _read_udp_header(p_buf, bytes_left, &pkt->udp_header);
	p_buf += bytes_read;
	bytes_left -= bytes_read;

	memcpy(pkt->udp_payload, p_buf, pkt->udp_header.length - bytes_read);
	pkt->udp_payload_len = pkt->udp_header.length - bytes_read;

	bytes_read = _read_rtp_header(p_buf, bytes_left, &pkt->rtp_header);
	p_buf += bytes_read;
	bytes_left -= bytes_read;

	if (pkt->rtp_header.version != 2) {
		free(pkt);
		return NULL;
	}

	return pkt;
}

static int _read_pkt_header(pcap_pkt_header_t *pkt_hdr, FILE *fp) {
	fread(&pkt_hdr->ts.tv_sec, 1, 4, fp);
	fread(&pkt_hdr->ts.tv_usec, 1, 4, fp);
	fread(&pkt_hdr->caplen, 1, 4, fp);
	fread(&pkt_hdr->len, 1, 4, fp);

	return 16;
}

static int _read_ethernet_header(uint8_t *buf, int buf_len, pcap_eth_header_t *eth)
{
	struct pcap_eth_header *tmp;
	int bytes_read = 0;

	memcpy(eth->dst_mac, buf, sizeof(eth->dst_mac));
	buf += sizeof(eth->dst_mac);

	memcpy(eth->src_mac, buf, sizeof(eth->src_mac));
	buf += sizeof(eth->src_mac);

	eth->type = ntohs(buf);
	buf += 2;

	bytes_read = sizeof(pcap_eth_header_t);

	return bytes_read;
}

static int _read_ip_header(uint8_t *buf, int buf_len, pcap_ip_header_t *ip)
{
	struct bitstream_elmt_t bs;

	bitstream_init(&bs, buf);

	ip->version = bitstream_u(&bs, 4);
	ip->header_length = bitstream_u(&bs, 4);
	ip->type_of_service = bitstream_u(&bs, 8);
	ip->total_length = bitstream_u(&bs, 16);

	ip->indentification = bitstream_u(&bs, 16);
	ip->flags = bitstream_u(&bs, 3);
	ip->fragment_offset = bitstream_u(&bs, 13);

	ip->ttl = bitstream_u(&bs, 8);
	ip->protocol = bitstream_u(&bs, 8);
	ip->header_checksum = bitstream_u(&bs, 16);

	ip->src_ip = ntohl(bitstream_u(&bs, 32));

	ip->dst_ip = ntohl(bitstream_u(&bs, 32));

	return ip->header_length * 4;
}

static int _read_udp_header(uint8_t *buf, int buf_len, pcap_udp_header_t *udp)
{
	struct bitstream_elmt_t bs;

	bitstream_init(&bs, buf);

	udp->src_port = bitstream_u(&bs, 16);
	udp->dst_port = bitstream_u(&bs, 16);

	udp->length = bitstream_u(&bs, 16);
	udp->checksum = bitstream_u(&bs, 16);

	return sizeof(pcap_udp_header_t);
}

static int _read_rtp_header(uint8_t *buf, int buf_len, rtp_fixed_header_t *rtp)
{
	int i;
	struct bitstream_elmt_t bs;

	bitstream_init(&bs, buf);

	rtp->version = bitstream_u(&bs, 2);
	rtp->padding = bitstream_u(&bs, 1);
	rtp->extension = bitstream_u(&bs, 1);
	rtp->csrc_count = bitstream_u(&bs, 4);
	rtp->marker = bitstream_u(&bs, 1);
	rtp->payload_type = bitstream_u(&bs, 7);
	rtp->sequence_number = bitstream_u(&bs, 16);

	if (rtp->version != 2)
	{
		printf("RTP fixed header not detected\n");
		return 0;
	}

	rtp->timestamp = bitstream_u(&bs, 32);

	rtp->ssrc = bitstream_u(&bs, 32);


	for (i = 0; i < rtp->csrc_count; i++)
	{
		rtp->csrc[i] = bitstream_u(&bs, 32);
	}

	return bs.byte_idx;
}

