/*
 * Simulator.cpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */
#include "Simulator.hpp"
#include "socket/Socket.hpp"
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

// _______________________________________________ PhysicalMedium Implementation

enum PhysicalMediumState {
	STOPPED,
	RUNNING,
	STOPPING
};

class PhysicalMedium_pimpl {
public:
	PhysicalMedium_pimpl(clockid_t clockidToUse) : clockidToUse(clockidToUse)
	{
		state = STOPPED;
	}
	enum PhysicalMediumState state;
	clockid_t clockidToUse;
	struct timespec curTime;
//	PacketArbitrator *pktArbitrator;
};


PhysicalMedium::PhysicalMedium(const char * name, int port, clockid_t clockidToUse)
{
	pimpl = new PhysicalMedium_pimpl(clockidToUse);
//	pktArbitrator = new PacketArbitrator(name, port);
}

PhysicalMedium::~PhysicalMedium() {
//	delete pktArbitrator;
	if (pimpl)
		delete pimpl;
}

void PhysicalMedium::startPacketArbitrator()
{
//	pktArbitrator->start();
}

void
PhysicalMedium::interval(long nanoseconds)
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
		rv = clock_nanosleep(pimpl->clockidToUse, TIMER_ABSTIME,
				     &request, &remain);

		if (rv != 0 && rv != EINTR) {
			/* Must be EFAULT or EINVAL which means some dodgy coding
			 * going on somewhere, exit cleanly */
			xlog(LOG_ALERT, "clock_nanosleep failed (%s)", strerror(rv));
			throw "clock_nanosleep unrecoverable error";
		}

		if (loglevel == LOG_DEBUG) {
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

void *PhysicalMedium::run() {

	pimpl->state = RUNNING;
	if (clock_gettime(pimpl->clockidToUse, &pimpl->curTime) == -1)
		throw "clock_gettime: unrecoverable error";

	xlog(LOG_NOTICE, "Initial CurTime value: %ld.%09ld\n",
		(long) pimpl->curTime.tv_sec, (long) pimpl->curTime.tv_nsec);
	do {
		interval(1000000000L);
		xlog(LOG_NOTICE, "CurTime value: %ld.%09ld\n",
			(long) pimpl->curTime.tv_sec, (long) pimpl->curTime.tv_nsec);

	} while(pimpl->state == RUNNING);
	xlog(LOG_NOTICE, "Network Simulator Stopped");
	pimpl->state = STOPPED;
	return 0;
}

// TODO: Implement
#if 0
void PhysicalMedium::stop() {
	pimpl->state = STOPPING;
}
#endif


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

	if (rc != 0)
		throw "PacketArbitrator failed to start";

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

clockid_t
get_highres_clock(void)
{
	struct timespec res;
	long resolution;

	if (clock_getres(CLOCK_REALTIME, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_REALTIME: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_REALTIME;
	}

	if (clock_getres(CLOCK_MONOTONIC, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_MONOTONIC: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_MONOTONIC;
	}

	if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_PROCESS_CPUTIME_ID: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_PROCESS_CPUTIME_ID;
	}

	return -1;
}


class NetworkSimulator_pimpl {
public:
	NetworkSimulator_pimpl(bool debugIn, int numMediums) : mediums(2)
	{
		debug = debugIn;
		stop = false;
		hanServer = NULL;
		airServer = NULL;
		clockidToUse = -1;
		unblocker = new SocketUnblocker();
	}
	~NetworkSimulator_pimpl()
	{
		delete unblocker;
	}
	bool debug;
	bool stop;
	clockid_t clockidToUse;
	std::vector<PhysicalMedium *> mediums;
	Socket *hanServer, *airServer;
	SocketUnblocker *unblocker;
	fd_set acceptFdSet;
};

NetworkSimulator::NetworkSimulator(bool debug)
{
	pimpl = new NetworkSimulator_pimpl(debug, 2);
	pimpl->clockidToUse = get_highres_clock();
	if (pimpl->clockidToUse == -1) {
		throw "Failed to get high resolution clock";
	}

	// TODO: Get ports from a config file or script.
	pimpl->mediums[0] = new PowerlineMedium(11555, pimpl->clockidToUse);
	pimpl->mediums[1] = new WirelessMedium(11556, pimpl->clockidToUse);

	pimpl->mediums[0]->startPacketArbitrator();
	pimpl->mediums[1]->startPacketArbitrator();
}

NetworkSimulator::~NetworkSimulator()
{
	if (pimpl)
		delete pimpl;
}

void
NetworkSimulator::start(void)
{

//	enum xccc_status accept_rc;
//	enum xccc_status handler_rv;
//	int rc;
//	struct xccc_ctrl_child * child_ctx = xccc->context;
	int rv;

	xlog(LOG_INFO, "Starting Xsilon 6lo Network Simulator\n");

	pimpl->hanServer = new Socket(AF_INET, SOCK_STREAM, 0);
	pimpl->airServer = new Socket(AF_INET, SOCK_STREAM, 0);

	/* Set socket as non blocking as we will use select and set the
	 * close on exec flag as if we fork to run a system command we don't
	 * want the new process to inherit this socket descriptor.
	 * Also set SO_REUSEADDR as we are going to bind to any address and
	 * want to reconnect if required. */
	pimpl->hanServer->setBlocking(false);
	pimpl->hanServer->setCloseOnExec(true);
	pimpl->hanServer->setReuseAddress(true);
	pimpl->hanServer->bindAnyAddress(HANADU_NODE_PORT);
	pimpl->hanServer->setPassive(20);

	pimpl->airServer->setBlocking(false);
	pimpl->airServer->setCloseOnExec(true);
	pimpl->airServer->setReuseAddress(true);
	pimpl->airServer->bindAnyAddress(WIRELESS_NODE_PORT);
	pimpl->airServer->setPassive(20);

	/* So we now have 2 sockets for accepting connection from either a
	 * hanadu or wireless node for the control channel.  Next we use
	 * select to multiplex both these sockets with the unblocker to
	 * actually do the accept.  Once accepted we use the client fd to
	 * create a new Socket that can be tied to either a HanaduNode or
	 * WirelessNode and placed on the corresponding PhyiscalMedium
	 */
	do {
		int hanClient, airClient;

		rv = acceptConnections(&hanClient, &airClient);
		if(rv == ACCEPT_OK) {
			//TODO: Create Client socket and associate with medium

			//socket_set_close_on_exec(child_ctx->cli_sockfd, true);
		}
	} while (!pimpl->stop && rv != ACCEPT_UNBLOCK);

	/* Clean up */
	delete pimpl->hanServer;
	delete pimpl->airServer;

}

void
NetworkSimulator::stop(void) {
	//TODO: Stop arbitrators
	//pimpl->mediums[0]->stopPacketArbitrator();
	//pimpl->mediums[1]->stopPacketArbitrator();
	pimpl->stop = true;
	//TODO: Unblock accept connections.
	pimpl->unblocker->unblock();
}

int
NetworkSimulator::setupAcceptFdSet()
{
	int fd_max = -1;
	int fd;

	FD_ZERO(&pimpl->acceptFdSet);

	fd = pimpl->hanServer->getSockFd();
	if(fd < 0) {
		xlog(LOG_ERR, "Invalid Hanadu socket fd (%d)", fd);
		throw "Invalid Hanadu Socket";
	}
	fd_max = fd;
	FD_SET(fd, &pimpl->acceptFdSet);

	fd = pimpl->airServer->getSockFd();
	if(fd < 0) {
		xlog(LOG_ERR, "Invalid Wireless socket fd (%d)", fd);
		throw "Invalid Wireless Socket";
	}
	if (fd > fd_max)
			fd_max = fd;
	FD_SET(fd, &pimpl->acceptFdSet);


	fd = pimpl->unblocker->getReadPipe();
	if(fd < 0) {
		xlog(LOG_WARNING, "Invalid unblock read pipe fd (%d)", fd);
		throw "Invalid Unblocker";
	}
	FD_SET(fd, &pimpl->acceptFdSet);
	if (fd > fd_max)
		fd_max = fd;

	return fd_max;
}

AcceptStatus
NetworkSimulator::acceptConnections(int *hanClient, int *airClient) {
	bool restart = false;
	AcceptStatus rv;

	*hanClient = -1;
	*airClient = -1;
	do {
		int fdmax;
		int count;

		restart = false;
		/*
		* Setup the Read file descriptor set which consists of Hanadu,
		* Wireless and the Unblocker file descriptors.
		*/
		fdmax = setupAcceptFdSet();
		if (fdmax > 0) {
			/* Wait for a connection to arrive in the queue, or for
			 * the unblock socket pipe. There is no timeout so it
			 * will wait forever. */
			xlog(LOG_INFO, "x6losim Socket server is waiting for connections ...");

			count = select(
					fdmax+1,
					&pimpl->acceptFdSet, /* accept will come in on read set */
					NULL,   /* No write set */
					NULL,   /* No exception set */
					NULL);  /* Block indefinitely */
			xlog(LOG_INFO, "Socket server listen finished.");

			if(count == -1) {
				if (errno == EINTR) {
					/* Select system call was interrupted by a signal so restart
					 * the system call */
					xlog(LOG_WARNING, "Select system call interrupted, restarting");
					restart = true;
					continue;
				}
				xlog(LOG_ERR, "select failed (%s)", strerror(errno));
				rv = ACCEPT_ERROR;
			} else if(count == 0) {
				xlog(LOG_ERR, "select timedout");
				rv = ACCEPT_TIMEOUT;
			} else {
				/* Connection or unblock */
				if(FD_ISSET(pimpl->hanServer->getSockFd(), &pimpl->acceptFdSet)) {
					/* Connection. */

					/* This shouldn't block as the select will have already informed us
					 * that a connection is waiting. */
					*hanClient = accept(pimpl->hanServer->getSockFd(), NULL, NULL);
					/* @todo check for EWOULDBLOCK, shouldn't happen though. */
					if (*hanClient < 0) {
						xlog(LOG_ERR,
						    "Failed to accept hanadu client connection (%s)",
						    strerror(errno));
						rv = ACCEPT_ERROR;
					} else {
						xlog(LOG_INFO, "Server Socket Hanadu Node Client Accepted");
					}
					count--;
				}
				if(FD_ISSET(pimpl->airServer->getSockFd(), &pimpl->acceptFdSet)) {
					/* Connection. */

					/* This shouldn't block as the select will have already informed us
					 * that a connection is waiting. */
					*airClient = accept(pimpl->airServer->getSockFd(), NULL, NULL);
					/* @todo check for EWOULDBLOCK, shouldn't happen though. */
					if (*airClient < 0) {
						xlog(LOG_ERR,
						    "Failed to accept wireless client connection (%s)",
						    strerror(errno));
						rv = ACCEPT_ERROR;
					} else {
						xlog(LOG_INFO, "Server Socket Wireless Node Client Accepted");
					}
					count--;
				}

				if(FD_ISSET(pimpl->unblocker->getReadPipe(),
						&pimpl->acceptFdSet)
				) {
					char buf[1];
					/* Task has been closed, read byte from pipe and set state to closing. */
					xlog(LOG_INFO, " Listen has been unblocked.");

					if(read(pimpl->unblocker->getReadPipe(), buf, 1) != 1) {
						xlog(LOG_ERR, "Failed to read unblock pipe.");
					}
					xlog(LOG_INFO, "Unblock pipe flushed.");
					if(*hanClient != -1) {
						close(*hanClient);
					}
					*hanClient = -1;
					if(*airClient != -1) {
						close(*airClient);
					}
					*airClient = -1;
					count--;
					/* Unblocking trumps previous status codes */
					rv = ACCEPT_UNBLOCK;
				}
				if(count != 0) {
					/* Problem. */
					if(*hanClient != -1) {
						close(*hanClient);
					}
					*hanClient = -1;
					if(*airClient != -1) {
						close(*airClient);
					}
					*airClient = -1;
					if (rv == ACCEPT_OK)
						rv = ACCEPT_ERROR;
					xlog(LOG_ERR, "Accept Failure, unknown fd from select");
				}
			}
		} else {
			xlog(LOG_ERR, "Accept Failure, invalid fdmax value");
			rv = ACCEPT_ERROR;
		}
	} while(restart);

	return rv;

}
