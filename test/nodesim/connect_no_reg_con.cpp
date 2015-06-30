#include "x6losim_interface.h"
#include "common.hpp"

#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>



// TODO Check those checksums :)
int
main()
{
	struct sockaddr_in serv_addr;
	int  sockfd;
	int n;
	char *server_reply[128];
	struct netsim_pkt_hdr *hdr = (struct netsim_pkt_hdr *)server_reply;
	uint16_t cksum,calculated_cksum;

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
	if((n = recv(sockfd , server_reply , 128 , 0)) < 0) {
		printf("recv failed");
		exit(EXIT_FAILURE);
	}
	/* Registration Request */
	printf("Msg Len  : %u\n", ntohs(hdr->len));
	printf("Msg Type : %u\n", ntohl(hdr->msg_type));
	printf("Interface: 0x%08x\n", ntohl(hdr->interface_version));
	printf("Node ID  : 0x%016llx\n",(long long unsigned int) ntohll(hdr->node_id));
	cksum = ntohs(hdr->cksum);
	hdr->cksum = 0;
	calculated_cksum = generate_checksum(hdr, n);
	printf("Rx CkSUM : 0x%04x\n", cksum);
	printf("CkSUM    : 0x%04x\n", calculated_cksum);
	assert(calculated_cksum == cksum);
	send_reg_confirm(sockfd, ntohll(hdr->node_id));

	sleep (3);

	send_dereg_req(sockfd, ntohll(hdr->node_id));
	if(recv(sockfd , server_reply , 128 , 0) < 0) {
		printf("recv failed");
		exit(EXIT_FAILURE);
	}

	printf("Msg Len  : %u\n", hdr->len);
	printf("Msg Type : %u\n", hdr->msg_type);
	printf("Interface: 0x%08x\n", hdr->interface_version);
	printf("Node ID  : 0x%016llx\n",(long long unsigned int) hdr->node_id);

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

	sleep (7);


	exit(EXIT_SUCCESS);
}
