#include "x6losim_interface.h"

#include <stdlib.h>
#include <byteswap.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


#ifndef ntohll
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntohll(uint64_t x) {
	return bswap_64(x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t ntohll(uint64_t x)
{
	return x;
}
#endif
#endif
#ifndef htonll
#define htonll ntohll
#endif

uint16_t
generate_checksum(void *msg, int msglen)
{
	int cksum = 0;
	uint16_t *p = (uint16_t *)msg;

	while (msglen > 1) {
		cksum += *p++;
		msglen -= 2;
	}
	if (msglen == 1)
		cksum += htons(*(unsigned char *)p << 8);

	cksum = (cksum >> 16) + (cksum & 0xffff);
	cksum += (cksum >> 16);
	return (~(uint16_t)cksum);
}


void
send_reg_confirm(int sockfd, uint64_t nodeId)
{
	struct node_to_netsim_registration_con_pkt reg_con;

	reg_con.hdr.len = htons(sizeof(reg_con));
	reg_con.hdr.msg_type = htons(MSG_TYPE_REG_CON);
	reg_con.hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	reg_con.hdr.node_id = htonll(nodeId);
	reg_con.hdr.cksum = 0;
	memset(reg_con.os, 0, sizeof(reg_con.os));
	memset(reg_con.os_version, 0, sizeof(reg_con.os_version));
	strcpy(reg_con.os, "linux");
	strcpy(reg_con.os_version, "4.1.0 rc4");
	reg_con.hdr.cksum = htons(generate_checksum(&reg_con, sizeof(reg_con)));

	send(sockfd, &reg_con, sizeof(reg_con), MSG_NOSIGNAL);
}


int
main()
{
	struct sockaddr_in serv_addr;
	int  sockfd;
	char *server_reply[128];
	struct netsim_pkt_hdr *hdr = (struct netsim_pkt_hdr *)server_reply;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		printf("socket() failed\n");
		exit(EXIT_FAILURE);
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(HANADU_NODE_PORT);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	//Connect to remote server
	if (connect(sockfd , (struct sockaddr *)&serv_addr , sizeof(serv_addr)) < 0) {
		perror("connect failed. Error");
		exit(EXIT_FAILURE);
	}

	printf("Connected to Hanadu NetSim Server\n");
	//Receive a reply from the server
	if(recv(sockfd , server_reply , 128 , 0) < 0) {
		printf("recv failed");
		exit(EXIT_FAILURE);
	}

	printf("Msg Len  : %u\n", hdr->len);
	printf("Msg Type : %u\n", hdr->msg_type);
	printf("Interface: 0x%08x\n", hdr->interface_version);
	printf("Node ID  : 0x%016llx\n",(long long unsigned int) hdr->node_id);

	send_reg_confirm(sockfd, ntohll(hdr->node_id));

	sleep (3);

	close(sockfd);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		printf("socket() failed\n");
		exit(EXIT_FAILURE);
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(HANADU_NODE_PORT);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	//Connect to remote server
	if (connect(sockfd , (struct sockaddr *)&serv_addr , sizeof(serv_addr)) < 0) {
		perror("connect failed. Error");
		exit(EXIT_FAILURE);
	}

	printf("Connected to Hanadu HetSim Server\n");
	//Receive a reply from the server
	if(recv(sockfd , server_reply , 128 , 0) < 0) {
		printf("recv failed");
		exit(EXIT_FAILURE);
	}

	printf("Msg Len  : %u\n", hdr->len);
	printf("Msg Type : %u\n", hdr->msg_type);
	printf("Interface: 0x%08x\n", hdr->interface_version);
	printf("Node ID  : 0x%016llx\n",(long long unsigned int) hdr->node_id);

	sleep (7);


	exit(EXIT_SUCCESS);
}
