#include "x6losim_interface.h"
#include "common.hpp"

#include <assert.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

class NodeSim_pimpl
{
public:
	NodeSim_pimpl()
	{
		sockfd = -1;
		nodeId = -1;
	}
public:
	int  sockfd;
	uint64_t nodeId;
	char replyBuffer[256];

};

NodeSim::NodeSim()
{
	pimpl = new NodeSim_pimpl();
}

NodeSim::~NodeSim()
{
	if (pimpl)
		delete pimpl;
}

void
NodeSim::connectNetSim()
{
	struct sockaddr_in serv_addr;

	pimpl->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(pimpl->sockfd < 0)
		throw "socket() failed\n";

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(HANADU_NODE_PORT);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	//Connect to remote server
	if (connect(pimpl->sockfd , (struct sockaddr *)&serv_addr , sizeof(serv_addr)) < 0)
		throw "connect failed. Error";

	printf("Connected to Hanadu NetSim Server\n");

}


void NodeSim::sendRegCon()
{
	struct node_to_netsim_registration_con_pkt reg_con;

	reg_con.hdr.len = htons(sizeof(reg_con));
	reg_con.hdr.msg_type = htons(MSG_TYPE_REG_CON);
	reg_con.hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	reg_con.hdr.node_id = htonll(pimpl->nodeId);
	reg_con.hdr.cksum = 0;
	memset(reg_con.os, 0, sizeof(reg_con.os));
	memset(reg_con.os_version, 0, sizeof(reg_con.os_version));
	strcpy(reg_con.os, "linux");
	strcpy(reg_con.os_version, "4.1.0 rc4");
	reg_con.hdr.cksum = htons(generate_checksum(&reg_con, sizeof(reg_con)));

	send(pimpl->sockfd, &reg_con, sizeof(reg_con), MSG_NOSIGNAL);

}

void NodeSim::sendDeregReq()
{
	struct node_to_netsim_deregistration_req_pkt dereg_req;

	dereg_req.hdr.len = htons(sizeof(dereg_req));
	dereg_req.hdr.msg_type = htons(MSG_TYPE_DEREG_REQ);
	dereg_req.hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	dereg_req.hdr.node_id = htonll(pimpl->nodeId);
	dereg_req.hdr.cksum = 0;
	dereg_req.hdr.cksum = htons(generate_checksum(&dereg_req, sizeof(dereg_req)));

	send(pimpl->sockfd, &dereg_req, sizeof(dereg_req), MSG_NOSIGNAL);
}

void NodeSim::sendCcaReq()
{
	struct node_to_netsim_cca_req_pkt cca_req;

	cca_req.hdr.len = htons(sizeof(cca_req));
	cca_req.hdr.msg_type = htons(MSG_TYPE_CCA_REQ);
	cca_req.hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	cca_req.hdr.node_id = htonll(pimpl->nodeId);
	cca_req.hdr.cksum = 0;
	cca_req.hdr.cksum = htons(generate_checksum(&cca_req, sizeof(cca_req)));

	send(pimpl->sockfd, &cca_req, sizeof(cca_req), MSG_NOSIGNAL);

}

void NodeSim::sendTxDataInd(uint64_t sourceAddr, uint16_t psduLen,
			    uint8_t repCode, int8_t txPower, uint8_t ccaMode)
{
	size_t txDataIndLen = sizeof(struct netsim_data_ind_pkt)- 1 + psduLen;
	struct netsim_data_ind_pkt *txDataInd =
			(struct netsim_data_ind_pkt *)malloc(txDataIndLen);

	txDataInd->hdr.len = htons(txDataIndLen);
	txDataInd->hdr.msg_type = htons(MSG_TYPE_TX_DATA_IND);
	txDataInd->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	txDataInd->hdr.node_id = htonll(pimpl->nodeId);

	txDataInd->source_addr = htonll(sourceAddr);
	txDataInd->psdu_len = htons(psduLen);
	txDataInd->rep_code = repCode;
	txDataInd->tx_power = -txPower;
	txDataInd->cca_mode = ccaMode;
	txDataInd->rssi = 0;

	//TODO: Fill in data
	for(int i=0; i<psduLen; i++) {
		txDataInd->pktData[i] = i;
	}

	txDataInd->hdr.cksum = 0;
	txDataInd->hdr.cksum = htons(generate_checksum(txDataInd, txDataIndLen));

	send(pimpl->sockfd, txDataInd, txDataIndLen, MSG_NOSIGNAL);

	free(txDataInd);
}

void NodeSim::readMsg()
{
	int n;
	uint16_t cksum, calculated_cksum;
	struct netsim_pkt_hdr *hdr = (struct netsim_pkt_hdr *)pimpl->replyBuffer;

	//Receive a reply from the server
	if((n = recv(pimpl->sockfd , pimpl->replyBuffer , 128 , 0)) < 0)
		throw "recv failed";

	printf("Msg Len  : %u\n", ntohs(hdr->len));
	printf("Msg Type : %u\n", ntohs(hdr->msg_type));
	printf("Interface: 0x%08x\n", ntohl(hdr->interface_version));
	printf("Node ID  : 0x%016llx\n",(long long unsigned int) ntohll(hdr->node_id));

	cksum = ntohs(hdr->cksum);
	hdr->cksum = 0;
	calculated_cksum = generate_checksum(hdr, n);
	printf("Rx CkSUM : 0x%04x\n", cksum);
	printf("CkSUM    : 0x%04x\n", calculated_cksum);
	assert(calculated_cksum == cksum);

	switch(ntohs(hdr->msg_type)) {
	case MSG_TYPE_REG_REQ:
		pimpl->nodeId = ntohll(hdr->node_id);
		break;
	case MSG_TYPE_DEREG_REQ:
		break;
	case MSG_TYPE_DEREG_CON:
		break;
	case MSG_TYPE_CCA_CON:
	{
		struct netsim_to_node_cca_con_pkt *ccaCon =
			(struct netsim_to_node_cca_con_pkt *)pimpl->replyBuffer;
		printf("Received CCA Con with result %d\n", ccaCon->result);
		break;
	}
	case MSG_TYPE_TX_DATA_IND:
		break;
	case MSG_TYPE_TX_DONE_IND:
		break;

	case MSG_TYPE_CCA_REQ:
	case MSG_TYPE_REG_CON:
	default:
		throw "Invalid received msg type";
	}

}

