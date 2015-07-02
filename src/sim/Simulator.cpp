/*
 * Simulator.cpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */
#include "Simulator.hpp"
#include "socket/Socket.hpp"
#include "log/log.hpp"
#include "utils/time.hpp"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>


// _____________________________________________ NetworkSimulator Implementation

class NetworkSimulator_pimpl {
public:
	NetworkSimulator_pimpl(bool debugIn)
	{
		debug = debugIn;
		stop = false;
		hanServer = NULL;
		airServer = NULL;
		// TODO: Get ports from a config file or script.
		plcMedium = new PowerlineMedium(HANADU_NODE_PORT, clockidToUse);
		wlMedium = new WirelessMedium(WIRELESS_NODE_PORT, clockidToUse);
	}
	~NetworkSimulator_pimpl()
	{
		delete plcMedium;
		delete wlMedium;
	}
	bool debug;
	bool stop;
	static clockid_t clockidToUse;
	PowerlineMedium *plcMedium;
	WirelessMedium *wlMedium;

	ServerSocket *hanServer, *airServer;
	fd_set acceptFdSet;
};
clockid_t NetworkSimulator_pimpl::clockidToUse = -1;

NetworkSimulator::NetworkSimulator(bool debug)
{
	NetworkSimulator_pimpl::clockidToUse = get_highres_clock();
	pimpl = new NetworkSimulator_pimpl(debug);
	if (pimpl->clockidToUse == -1) {
		throw "Failed to get high resolution clock";
	}

	pimpl->plcMedium->start();
	pimpl->wlMedium->start();
}

NetworkSimulator::~NetworkSimulator()
{
	if (pimpl->plcMedium)
		pimpl->plcMedium->stop();

	if (pimpl->wlMedium)
		pimpl->wlMedium->stop();

	/* TODO: Wait for physical medium threads to stop */
	pimpl->plcMedium->waitForExit();
	pimpl->wlMedium->waitForExit();

	if (pimpl)
		delete pimpl;
}

clockid_t
NetworkSimulator::getClockId()
{
	return NetworkSimulator_pimpl::clockidToUse;
}
SocketUnblocker& NetworkSimulator::getUnblocker()
{
	// Guaranteed to be destroyed. Instantiated on first use.
	static SocketUnblocker instance;
	return instance;
}

void
NetworkSimulator::start(void)
{
	int rv;
	int errCount = 0;

	xlog(LOG_INFO, "Starting Xsilon 6lo Network Simulator\n");

	pimpl->hanServer = new ServerSocket(HANADU_NODE_PORT);
	pimpl->airServer = new ServerSocket(WIRELESS_NODE_PORT);

	/* Set socket as non blocking as we will use select and set the
	 * close on exec flag as if we fork to run a system command we don't
	 * want the new process to inherit this socket descriptor.
	 * Also set SO_REUSEADDR as we are going to bind to any address and
	 * want to reconnect if required. */
	pimpl->hanServer->setBlocking(false);
	pimpl->hanServer->setCloseOnExec(true);
	pimpl->hanServer->setReuseAddress(true);
	pimpl->hanServer->bindAnyAddress();
	pimpl->hanServer->setPassive(20);

	pimpl->airServer->setBlocking(false);
	pimpl->airServer->setCloseOnExec(true);
	pimpl->airServer->setReuseAddress(true);
	pimpl->airServer->bindAnyAddress();
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
			/* Reset error count. */
			errCount = 0;
			// Create Client socket and associate with medium
			if (hanClient != -1) {
				HanaduDeviceNode * node = new HanaduDeviceNode(hanClient);
				pimpl->plcMedium->addNode(node);
			}
			if (airClient != -1) {
				WirelessDeviceNode * node = new WirelessDeviceNode(airClient);
				pimpl->wlMedium->addNode(node);
			}

			//socket_set_close_on_exec(child_ctx->cli_sockfd, true);
		} else if(rv == ACCEPT_TIMEOUT) {
			errCount = 0;

		} else if(rv == ACCEPT_ERROR) {
			errCount++;
			if (errCount > 5) {
				delete pimpl->hanServer;
				delete pimpl->airServer;
				throw "Fatal error in acceptConnections";
			}
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
	// Unblock accept connections and current socket reads.
	NetworkSimulator::getUnblocker().unblock();
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


	fd = NetworkSimulator::getUnblocker().getReadPipe();
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
	AcceptStatus rv = ACCEPT_OK;

	*hanClient = -1;
	*airClient = -1;
	do {
		int fdmax;
		int count;

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
					continue;
				}
				xlog(LOG_ERR, "select failed (%s)", strerror(errno));
				rv = ACCEPT_ERROR;
			} else if(count == 0) {
				/* This shouldn't happen */
				xlog(LOG_ERR, "select timedout");
				rv = ACCEPT_TIMEOUT;
			} else {
				/* Connection or unblock */
				if(FD_ISSET(pimpl->hanServer->getSockFd(), &pimpl->acceptFdSet)) {
					/* Connection. */

					/* This shouldn't block as the select will have already informed us
					 * that a connection is waiting. */
					*hanClient = accept(pimpl->hanServer->getSockFd(), NULL, NULL);
					if (*hanClient < 0) {
						assert(*hanClient != EWOULDBLOCK);
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
					if (*airClient < 0) {
						assert(*hanClient != EWOULDBLOCK);
						xlog(LOG_ERR,
						    "Failed to accept wireless client connection (%s)",
						    strerror(errno));
						rv = ACCEPT_ERROR;
					} else {
						xlog(LOG_INFO, "Server Socket Wireless Node Client Accepted");
					}
					count--;
				}

				if(FD_ISSET(NetworkSimulator::getUnblocker().getReadPipe(),
						&pimpl->acceptFdSet)
				) {
					char buf[1];
					/* Task has been closed, read byte from pipe and set state to closing. */
					xlog(LOG_INFO, " Listen has been unblocked.");

					if(read(NetworkSimulator::getUnblocker().getReadPipe(), buf, 1) != 1) {
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
	} while(0);

	return rv;

}

