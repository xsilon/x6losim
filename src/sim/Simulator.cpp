/*
 * Simulator.cpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */
#include "x6losim_interface.h"
#include "Simulator.hpp"
#include "log/log.hpp"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
 #include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <vector>

// _____________________________________________ PacketArbitrator Implementation

class PacketArbitrator_pimpl {
public:
	bool running;
	const char * name;
	struct {
		pthread_t thread;
		int sockfd;
		int port;
		struct sockaddr_in servaddr;
	} rx;
};

PacketArbitrator::PacketArbitrator(const char * name, int port)
{
	pimpl = new PacketArbitrator_pimpl();
	pimpl->name = strdup(name);
	pimpl->rx.port = port;
}

PacketArbitrator::~PacketArbitrator()
{
	if (pimpl) {
		if (pimpl->name)
			free((void *)pimpl->name);
		delete pimpl;
	}
}

int
PacketArbitrator::start()
{
	int rc;

	rc = pthread_create(&pimpl->rx.thread, NULL,
			PacketArbitrator::run_helper, (void *)this);
	return 0;
}

void *
PacketArbitrator::run()
{
	uint32_t pkts_rx = 0;
	uint8_t *pkt_buf;

	pimpl->rx.sockfd=socket(AF_INET,SOCK_DGRAM,0);
	if (pimpl->rx.sockfd == -1) {
		xlog(LOG_ERR, "Server socket creation failed (%s)", strerror(errno));
		throw "Server socket failed";
	}

	bzero(&pimpl->rx.servaddr,sizeof(pimpl->rx.servaddr));
	pimpl->rx.servaddr.sin_family = AF_INET;
	pimpl->rx.servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	pimpl->rx.servaddr.sin_port=htons(pimpl->rx.port);
	if (bind(pimpl->rx.sockfd, (struct sockaddr *)&pimpl->rx.servaddr,
			sizeof(pimpl->rx.servaddr)) != 0
	) {
		xlog(LOG_ERR, "Bind failed for Netsim server socket (%s)",
			strerror(errno));
		throw "Server socket failed";
	}
	xlog(LOG_INFO, "%s: UDP ServerSocket listening on port %d", pimpl->name,
			pimpl->rx.port);

	pimpl->running = true;

	pkt_buf = (uint8_t *)malloc(NETSIM_PKT_MAX_SZ);
	while (pimpl->running) {
		struct sockaddr_in cliaddr;
		socklen_t len;
		int n;

		len = sizeof(cliaddr);
		n = recvfrom(pimpl->rx.sockfd, pkt_buf, NETSIM_PKT_MAX_SZ, 0,
					 (struct sockaddr *)&cliaddr, &len);

		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
		} else if (n == 0) {
			continue;
		}

		//x6losim_recv(sim.pktbuf, n);

		xlog(LOG_INFO, "%d: Received packet %d from %s", n, pkts_rx,
			 inet_ntoa(cliaddr.sin_addr));
		pkts_rx++;
	}

	close(pimpl->rx.sockfd);
	free(pkt_buf);
	pthread_exit(NULL);
}

// _____________________________________________ NetworkSimulator Implementation

class NetworkSimulator_pimpl {
public:
	NetworkSimulator_pimpl(bool debugIn, int numMediums) : mediums(2)
	{
		debug = debugIn;
	}
	bool debug;
	std::vector<PhysicalMedium *> mediums;
};

NetworkSimulator::NetworkSimulator(bool debug)
{
	pimpl = new NetworkSimulator_pimpl(debug, 2);

	// TODO: Get ports from a config file or script.
	pimpl->mediums[0] = new PowerlineMedium(11555);
	pimpl->mediums[1] = new WirelessMedium(11556);

	pimpl->mediums[0]->startPacketArbitrator();
	pimpl->mediums[1]->startPacketArbitrator();
}

NetworkSimulator::~NetworkSimulator()
{
	if (pimpl)
		delete pimpl;
}

int
NetworkSimulator::start(void)
{
	sleep(2);
	return 0;
}
