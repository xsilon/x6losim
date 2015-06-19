/*
 * Simulator.cpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */
#include "Simulator.hpp"
#include "log/log.hpp"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
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

		NetSimPktList pktList;
		pthread_mutex_t pktListMutex;

		struct stats {
			uint32_t pkts_rx;
			uint32_t pkts_ok;
			uint32_t pkts_dropped;
		} stats;
} rx;
};

PacketArbitrator::PacketArbitrator(const char * name, int port)
{
	pimpl = new PacketArbitrator_pimpl();
	pimpl->name = strdup(name);
	pimpl->rx.port = port;
	pimpl->rx.stats.pkts_rx = 0;
	pimpl->rx.stats.pkts_ok = 0;
	pimpl->rx.stats.pkts_dropped = 0;

	pthread_mutex_init(&pimpl->rx.pktListMutex, NULL);
}

PacketArbitrator::~PacketArbitrator()
{
	// @todo implement select and use a pipe to unblock socket read.
	// under mutex free all packets in list
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
	NetSimPacket *pkt;

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

	while (pimpl->running) {
		struct sockaddr_in cliaddr;
		socklen_t len;
		int n;
		struct netsim_pkt_hdr *hdr;

		pkt = new NetSimPacket();
		len = sizeof(cliaddr);
		/* TODO: ensure we receive a full 256 byte payload */
		n = recvfrom(pimpl->rx.sockfd, pkt->buf(), pkt->bufSize(), 0,
					 (struct sockaddr *)&cliaddr, &len);

		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			//@todo other ERR codes
		} else if (n == 0) {
			continue;
		}

		hdr = (struct netsim_pkt_hdr *)pkt->buf();
		if (hdr->interface_version == NETSIM_INTERFACE_VERION) {
			pthread_mutex_lock(&pimpl->rx.pktListMutex);
			pimpl->rx.pktList.push_back(pkt);
			pthread_mutex_unlock(&pimpl->rx.pktListMutex);
			xlog(LOG_INFO, "%d: Received packet %d from %s", n, pimpl->rx.stats.pkts_rx,
				 inet_ntoa(cliaddr.sin_addr));

			pimpl->rx.stats.pkts_ok++;
		} else {
			xlog(LOG_ERR, "%d: Dropping packet %d from %s", n, pimpl->rx.stats.pkts_rx,
				inet_ntoa(cliaddr.sin_addr));
			xlog(LOG_ERR, "Interface version mismatch pkt(0x%08x) != sim(0x%08x)",
				hdr->interface_version, NETSIM_INTERFACE_VERION);

			pimpl->rx.stats.pkts_dropped++;
		}
		pimpl->rx.stats.pkts_rx++;
	}

	close(pimpl->rx.sockfd);
	pthread_exit(NULL);
}

void
PacketArbitrator::getCapturedPackets(NetSimPktList &pktList)
{
	std::list<NetSimPacket *>::iterator iter;
	// Copy packets into the passed list under the mutex so other packets
	// can't be received
	pthread_mutex_lock(&pimpl->rx.pktListMutex);
	for(iter = pimpl->rx.pktList.begin(); iter != pimpl->rx.pktList.end(); ) {
		pktList.push_back(*iter);
		iter = pimpl->rx.pktList.erase(iter);
	}
	pthread_mutex_unlock(&pimpl->rx.pktListMutex);
	assert(pimpl->rx.pktList.empty());
}

// _____________________________________________ NetworkSimulator Implementation

enum NetSimState {
	STOPPED,
	RUNNING,
	STOPPING
};

class NetworkSimulator_pimpl {
public:
	NetworkSimulator_pimpl(bool debugIn, int numMediums) : mediums(2)
	{
		debug = debugIn;
		state = STOPPED;
	}
	enum NetSimState state;
	bool debug;
	clockid_t clockid_to_use;
	struct timespec curTime;
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


clockid_t
get_highres_clock(void)
{
	struct timespec res;
	long resolution;

	if (clock_getres(CLOCK_REALTIME, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_ERR, "CLOCK_REALTIME: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_REALTIME;
	}

	if (clock_getres(CLOCK_MONOTONIC, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_ERR, "CLOCK_MONOTONIC: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_MONOTONIC;
	}

	if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_ERR, "CLOCK_PROCESS_CPUTIME_ID: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_PROCESS_CPUTIME_ID;
	}

	return -1;
}

void
NetworkSimulator::interval(long nanoseconds)
{
	struct timeval start, finish;
	struct timespec request, remain;
	int rv;

	pimpl->curTime.tv_nsec += nanoseconds;
	if (pimpl->curTime.tv_nsec >= 1000000000) {
		pimpl->curTime.tv_sec += pimpl->curTime.tv_nsec / 1000000000;
		pimpl->curTime.tv_nsec %= 1000000000;
	}
	request = pimpl->curTime;

	if (gettimeofday(&start, NULL) == -1)
		throw "gettimeofday unrecoverable error";
	for (;;) {
		rv = clock_nanosleep(pimpl->clockid_to_use, TIMER_ABSTIME,
				     &request, &remain);

		if (rv != 0 && rv != EINTR) {
			/* Must be EFAULT or EINVAL which means some dodgy coding
			 * going on somewhere, exit cleanly */
			xlog(LOG_ALERT, "clock_nanosleep failed (%s)", strerror(rv));
			throw "clock_nanosleep unrecoverable error";
		}

		if (pimpl->debug) {
			if (gettimeofday(&finish, NULL) == -1)
				throw "gettimeofday unrecoverable error";
			xlog(LOG_DEBUG, "Slept: %.6f secs",
				finish.tv_sec - start.tv_sec
				+ (finish.tv_usec - start.tv_usec) / 1000000.0);
		}

		if (rv == 0)
			break; /* sleep completed */

		xlog(LOG_DEBUG, "... Remaining: %ld.%09ld",
			(long) remain.tv_sec, remain.tv_nsec);
		request = remain;

		xlog(LOG_DEBUG, "... Restarting\n");
	}

}

int
NetworkSimulator::start(void)
{
	/* TODO: Use clock_getres to check timer resolution */
	pimpl->clockid_to_use = get_highres_clock();
	if (pimpl->clockid_to_use == -1) {
		throw "Failed to get high resolution clock";
	}

	pimpl->state = RUNNING;
        if (clock_gettime(pimpl->clockid_to_use, &pimpl->curTime) == -1)
            throw "clock_gettime: unrecoverable error";

        xlog(LOG_NOTICE, "Initial CurTime value: %ld.%09ld\n",
                (long) pimpl->curTime.tv_sec, (long) pimpl->curTime.tv_nsec);
	do {
		interval(1000000000L);
	        xlog(LOG_NOTICE, "CurTime value: %ld.%09ld\n",
	                (long) pimpl->curTime.tv_sec, (long) pimpl->curTime.tv_nsec);

	} while(pimpl->state == RUNNING);
	xlog(LOG_NOTICE, "Network Simulator Stopped");
	pimpl->state == STOPPED;
	return 0;
}

int
NetworkSimulator::stop(void) {
	//TODO: Stop arbitrators
	//pimpl->mediums[0]->stopPacketArbitrator();
	//pimpl->mediums[1]->stopPacketArbitrator();
	pimpl->state = STOPPING;
}
