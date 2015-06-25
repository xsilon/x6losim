#include "Simulator.hpp"
#include "socket/Socket.hpp"
#include "log/log.hpp"
#include "utils/time.hpp"

#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>

#include <unordered_map>
#include <list>
#include <mutex>


// TODO: Remove PacketArbitrator class once we have what we need out of it.
#if 0
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
#if 0
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
		if (hdr->interface_version == NETSIM_INTERFACE_VERSION) {
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
#endif
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

#endif

// _______________________________________________ PhysicalMedium Implementation

enum PhysicalMediumState {
	STOPPED,
	IDLE,
	TX_802514_FRAME,
	STOPPING
};

class PhysicalMedium_pimpl {
public:
	PhysicalMedium_pimpl(clockid_t clockidToUse, const char * nameIn) : clockidToUse(clockidToUse)
	{
		state = STOPPED;
		name = strdup(nameIn);
		poller.epfd = epoll_create1(EPOLL_CLOEXEC);
		thread = -1;
		if (poller.epfd < 0)
			throw "epoll_create error";
		/*
		 * SIGEV_NONE is supposed to prevent signal delivery, but it doesn't.
		 * Set signo to SIGSTOP to make the received signal obvious but
		 * harmless.
		 */
		memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_NONE;
		sev.sigev_signo = SIGSTOP;
		if (timer_create(clockidToUse, &sev, &timer) == -1) {
			xlog(LOG_ERR, "timer_create fail (%d:%s)", errno,
					strerror(errno));
			throw "PhysicalMedium::interval: failed to create timer";
		}
	}
	~PhysicalMedium_pimpl()
	{
		close(poller.epfd);
		timer_delete(timer);
		free(name);
	}
	enum PhysicalMediumState state;
	clockid_t clockidToUse;
	timer_t timer;
	struct sigevent sev;
	struct timespec curTime;
	std::unordered_map<uint64_t, DeviceNode *> nodeHashMap;
	std::list<DeviceNode *> unregList;
	std::mutex unregListMutex;
	pthread_t thread;
	char * name;

	struct {
		int epfd;

		struct epoll_event events[EPOLL_MAX_EVENTS];
	} poller;
};


PhysicalMedium::PhysicalMedium(const char * name, int port, clockid_t clockidToUse)
{
	pimpl = new PhysicalMedium_pimpl(clockidToUse, name);
}

PhysicalMedium::~PhysicalMedium() {
	if (pimpl)
		delete pimpl;
}

void PhysicalMedium::startPacketArbitrator()
{
//	pktArbitrator->start();
}

void PhysicalMedium::addNode(DeviceNode* node)
{
	struct epoll_event ev;
	int rv;

	xlog(LOG_DEBUG, "%s: Adding Node ID (0x%016llx) to registration list",
		pimpl->name, node->getNodeId());
	ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	ev.data.fd = node->getSocketFd();

	/* epoll is thread safe so we can add the new node's socket fd to the
	 * interest list and not upset the epoll_wait that the main thread
	 * of this class may be calling.
	 */
	rv = epoll_ctl(pimpl->poller.epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
	if (rv < 0) {
		if (rv == EEXIST) {
			// already registered
			throw "epoll_ctl: Already registered";
		} else {
			throw "epoll_ctl: Error";
		}
	}
	pimpl->unregListMutex.lock();
	pimpl->unregList.push_back(node);
	pimpl->unregListMutex.unlock();
	//TODO: Move to the part where the node is registered.
	pimpl->nodeHashMap.insert(std::make_pair(node->getNodeId(), node));
}

void PhysicalMedium::removeNode(DeviceNode* node)
{

}

#if 0
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
//			xlog(LOG_DEBUG, "Slept: %.6f secs",
//				finish.tv_sec - start.tv_sec
//				+ (finish.tv_usec - start.tv_usec) / 1000000.0);
		}

		if (rv == 0)
			break; /* sleep completed */

		xlog(LOG_DEBUG, "... Remaining: %ld.%09ld",
			(long) remain.tv_sec, remain.tv_nsec);
		request = remain;

		xlog(LOG_DEBUG, "... Restarting\n");
	}

}
#endif

void
PhysicalMedium::processPollerEvents(int numEvents)
{
	int i;
	struct epoll_event *evp;

	assert(numEvents <= EPOLL_MAX_EVENTS);

	evp = pimpl->poller.events;
	for (i = 0; i < numEvents; i++) {

		evp++;
	}
}

void
PhysicalMedium::checkNodeRegistrationTimeout()
{
	std::list<DeviceNode *>::iterator iter;
	/* Iterate through unreg list and remove and delete all nodes that
	 * have timedout. */
	pimpl->unregListMutex.lock();
	iter = pimpl->unregList.begin();
	while (iter != pimpl->unregList.end()) {
		DeviceNode *node = *iter;

		assert(node->getState() == DEV_NODE_STATE_REGISTERING);
		if (node->registrationTimeout()) {
			xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) to registration list",
					pimpl->name, node->getNodeId());
			pimpl->unregList.erase(iter++);
			/* This will delete timer and close socket */
			delete node;
		} else {
			iter++;
		}

	}
	pimpl->unregListMutex.unlock();
}

/*
 * waitms: -1 for block until event occurs
 *          0 perform non blocking check
 *          >0 Timeout in milliseconds.
 */
void
PhysicalMedium::interval(int waitms)
{
	int rv;
	struct itimerspec ts;
	struct itimerspec ts_left;

	/* One shot timer, armed with the time after the interval specified */
	timespec_add_ms(&pimpl->curTime, waitms);
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value  = pimpl->curTime;


	/* As we are using a timer any adjustments while this asbsolute timer
	 * is armed will also be adjusted. */
	rv = timer_settime(pimpl->timer, TIMER_ABSTIME, &ts, NULL);
	if (rv == -1)
		throw "PhysicalMedium::interval: failed to arm timer";
	do {
		rv = epoll_wait(pimpl->poller.epfd, pimpl->poller.events,
				EPOLL_MAX_EVENTS, waitms);

		if (rv == -1) {
			if (errno != EINTR)
				/* Not EINTR so we do have a problem */
				throw "PhysicalMedium::interval: epoll failure";
		} else {
			//rv is 0 or number of file descriptors to process.
			if (rv) {
				processPollerEvents(rv);
			}
		}

		if (timer_gettime(pimpl->timer, &ts_left) == -1)
			throw "PhysicalMedium::interval: failed to get timer";

		// Readjust wait ms based on time left.
		waitms = (ts_left.it_value.tv_sec * 1000)
				+ (ts_left.it_value.tv_nsec /  1000000);
	} while(rv > 0 && waitms > 0);
}

int
PhysicalMedium::start()
{
	int rc;

	rc = pthread_create(&pimpl->thread, NULL,
			PhysicalMedium::run_helper, (void *)this);

	if (rc != 0)
		throw "PacketArbitrator failed to start";

	return 0;
}

void PhysicalMedium::waitForExit()
{
	void *res;
	xlog(LOG_NOTICE, "%s: Wait for exit from Physical Medium thread", pimpl->name);
	pthread_join(pimpl->thread, &res);
	xlog(LOG_NOTICE, "%s: Exit from Physical Medium thread", pimpl->name);
}

void *PhysicalMedium::run() {

	pimpl->state = IDLE;
	if (clock_gettime(pimpl->clockidToUse, &pimpl->curTime) == -1)
		throw "clock_gettime: unrecoverable error";

	do {
		if (pimpl->state == IDLE) {
			interval(1000);
		}
		/* Check for nodes that have expired registration period */
		checkNodeRegistrationTimeout();
	} while(pimpl->state != STOPPING);
	xlog(LOG_NOTICE, "Network Simulator Stopped");
	pimpl->state = STOPPED;
	return 0;
}

void PhysicalMedium::stop()
{
	pimpl->state = STOPPING;
}


#if 0
void
processRegistrationConfirmation()
{
	SocketReadStatus readStatus;
	int bytesRead;

	/* Wait 5 seconds for reply otherwise node is deemed unreachable */
	readStatus = pimpl->socket->recvReply((char *)reply, replyMsgLen, 5000,
			&bytesRead, &NetworkSimulator::getUnblocker());
	switch (readStatus) {
	case SOCK_READ_OK:
		assert(replyMsgLen == bytesRead);
		if (reply->hdr.interface_version == NETSIM_INTERFACE_VERSION) {
			/* Copy Registration info out */
			memset(pimpl->os, 0, sizeof(pimpl->os));
			strncpy(pimpl->os, reply->os, strlen(reply->os));
			if (pimpl->os[sizeof(pimpl->os) - 1] != '\0')
				pimpl->os[sizeof(pimpl->os) - 1] = '\0';
			memset(pimpl->osVersion, 0, sizeof(pimpl->osVersion));
			strncpy(pimpl->osVersion, reply->os_version, strlen(reply->os_version));
			if (pimpl->osVersion[sizeof(pimpl->osVersion) - 1] != '\0')
				pimpl->osVersion[sizeof(pimpl->osVersion) - 1] = '\0';

			xlog(LOG_DEBUG, "Registering node 0x%16x (%s %s)",
					(uint64_t)pimpl->socket, pimpl->os,
					pimpl->osVersion);
		} else {
			success = false;
		}
		break;
	case SOCK_READ_CONN_CLOSED:
	case SOCK_READ_UNBLOCKED:
	case SOCK_READ_TIMEOUT:
	case SOCK_READ_ERROR:
		success = false;
		break;
	default:
		throw "Unknown read status";
	}
}
#endif

// ______________________________________________ PowerlineMedium Implementation

void PowerlineMedium::addNode(HanaduDeviceNode* node)
{
	PhysicalMedium::addNode(node);
}

void PowerlineMedium::removeNode(HanaduDeviceNode* node)
{
	PhysicalMedium::removeNode(node);
}

// _______________________________________________ WirelessMedium Implementation

void WirelessMedium::addNode(WirelessDeviceNode* node)
{
	PhysicalMedium::addNode(node);
}

void WirelessMedium::removeNode(WirelessDeviceNode* node)
{
	PhysicalMedium::removeNode(node);
}

