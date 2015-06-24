#include "x6losim_interface.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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


	sleep (6);


	exit(EXIT_SUCCESS);
}
