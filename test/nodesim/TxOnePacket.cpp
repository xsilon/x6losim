#include "x6losim_interface.h"
#include "common.hpp"

#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main()
{
	//Create Node Sim and connect
	NodeSim *node = new NodeSim();

	node->connectNetSim();

	// TODO: Expect Reg Req
	node->readMsg();

	node->sendRegCon();

	sleep(1);

	node->sendCcaReq();
	// TODO: Expect CCA confirm -> result == TRUE
	node->readMsg();

	node->sendTxDataInd(0x0102030405060708, 32, 48, -10, 4);

	//TODO: Read Tx Done Ind.
	sleep(1);

	node->mcastRxDataPacket();

	exit(EXIT_SUCCESS);
}
