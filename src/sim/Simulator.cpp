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
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <list>
#include <mutex>
#include <unordered_map>

#define EPOLL_MAX_EVENTS		(64)
#define REGISTRATION_TIME		(5)


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

int
compare_timespecs(const struct timespec* t1, const struct timespec* t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	else if (t1->tv_sec == t2->tv_sec) {
		if (t1->tv_nsec < t2->tv_nsec)
			return -1;
		else if (t1->tv_nsec == t2->tv_nsec)
			return 0;
		else
			return 1;
	}
	else
		return 1;
}

/*
 * Subtract the ‘struct timeval’ values X and Y,
 * storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
int
timespec_subtract(struct timespec *result, struct timespec *x, struct timespec *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_nsec < y->tv_nsec) {
		int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
		y->tv_nsec += 1000000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait. tv_nsec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = x->tv_nsec - y->tv_nsec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

void
timespec_add_ms(struct timespec *x, unsigned long ms)
{
	x->tv_nsec += ms * 1000000;
	if (x->tv_nsec >= 1000000000) {
		x->tv_sec += x->tv_nsec / 1000000000;
		x->tv_nsec %= 1000000000;
	}

}

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

// ___________________________________________________ DeviceNode Implementation

class DeviceNode_pimpl {
public:
	DeviceNode_pimpl(int sockfd) : sockfd(sockfd)
	{
		socket = new Socket(sockfd);
		state = DEV_NODE_STATE_UNREG;
		/*
		 * SIGEV_NONE is supposed to prevent signal delivery, but it doesn't.
		 * Set signo to SIGSTOP to make the received signal obvious but
		 * harmless.
		 */
		regTimerSigEvent.sigev_notify = SIGEV_NONE;
		regTimerSigEvent.sigev_signo = SIGSTOP;
	}
	~DeviceNode_pimpl()
	{
		delete socket;
	}
	int sockfd;
	Socket * socket;
	char os[32];
	char osVersion[32];
	DeviceNodeState state;
	static struct itimerspec regTimerSpec;

	struct sigevent regTimerSigEvent;
	timer_t regTimer;
};
struct itimerspec DeviceNode_pimpl::regTimerSpec = {
	.it_interval = {
		.tv_sec =  0,
		.tv_nsec = 0,
	},
	.it_value = {
		.tv_sec = REGISTRATION_TIME,
		.tv_nsec = 0
	}
};
//ts.it_interval.tv_sec = 0;
//ts.it_interval.tv_nsec = 0;
//ts.it_value.tv_sec = REGISTRATION_TIME;
//ts.it_value.tv_nsec = 0;


DeviceNode::DeviceNode(int sockfd)
{
	pimpl = new DeviceNode_pimpl(sockfd);
	pimpl->socket->setCloseOnExec(true);

}

DeviceNode::~DeviceNode()
{
	if (pimpl->state == DEV_NODE_STATE_REGISTERING) {
		timer_delete(pimpl->regTimer);
	}
	if (pimpl)
		delete pimpl;
}

uint64_t
DeviceNode::getNodeId()
{
	return (uint64_t)pimpl->socket;
}

int
DeviceNode::getSocketFd()
{
	return (uint64_t)pimpl->sockfd;
}

DeviceNodeState DeviceNode::getState()
{
	return pimpl->state;
}

bool
DeviceNode::sendRegistrationRequest()
{
	struct netsim_to_node_registration_req_pkt * msg;
	struct node_to_netsim_registration_con_pkt * reply;
	int msglen = sizeof(*msg);
	int replyMsgLen = sizeof(*reply);
	bool success = true;

	msg = (netsim_to_node_registration_req_pkt *)malloc(msglen);
	reply =  (node_to_netsim_registration_con_pkt *)malloc(replyMsgLen);

	msg->hdr.len = sizeof(*msg);
	msg->hdr.interface_version = NETSIM_INTERFACE_VERSION;
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = (uint64_t)pimpl->socket;

	/* Send registration request message, on return the caller will add
	 * to the relevant physical medium which will then process the
	 * confirm or remove if not received within a period of time. */
	pimpl->socket->sendMsg((char *)msg, msglen);

	/* Start registration timer */
	pimpl->state = DEV_NODE_STATE_REGISTERING;
	if (timer_create(NetworkSimulator::getClockId(),
			 &pimpl->regTimerSigEvent,
			 &pimpl->regTimer) == -1)
		throw "DeviceNode::sendRegistrationRequest: failed to create timer";
	if (timer_settime(pimpl->regTimer, 0, &pimpl->regTimerSpec, NULL) == -1)
		throw "DeviceNode::sendRegistrationRequest: failed to arm timer";

	free(msg);
	free(reply);

	return success;
}

bool
DeviceNode::registrationTimeout()
{
	itimerspec ts_left;

	if (timer_gettime(pimpl->regTimer, &ts_left) == -1)
		throw "PhysicalMedium::interval: failed to get timer";

	if (ts_left.it_value.tv_sec == 0 && ts_left.it_value.tv_nsec == 0)
		return true;
	else
		return false;
}


class HanaduDeviceNode_pimpl {
public:
	HanaduDeviceNode_pimpl()
	{
	}
	~HanaduDeviceNode_pimpl()
	{
	}
};

HanaduDeviceNode::HanaduDeviceNode(int sockfd) : DeviceNode(sockfd)
{
	pimpl = new HanaduDeviceNode_pimpl();
}

HanaduDeviceNode::~HanaduDeviceNode()
{
	if (pimpl)
		delete pimpl;
}

class WirelessDeviceNode_pimpl {
public:
	WirelessDeviceNode_pimpl()
	{
	}
	~WirelessDeviceNode_pimpl()
	{
	}
};

WirelessDeviceNode::WirelessDeviceNode(int sockfd) : DeviceNode(sockfd)
{
	pimpl = new WirelessDeviceNode_pimpl();
}

WirelessDeviceNode::~WirelessDeviceNode()
{
	if (pimpl)
		delete pimpl;
}

// _____________________________________________ NetworkSimulator Implementation

clockid_t
get_highres_clock(void)
{
	struct timespec res;
	long resolution;

	//affected by setting time and NTP
	if (clock_getres(CLOCK_REALTIME, &res) != -1) {
		resolution = (res.tv_sec * 1000000000L) + res.tv_nsec;
		xlog(LOG_NOTICE, "CLOCK_REALTIME: %ld ns\n", resolution);
		if (resolution == 1)
			return CLOCK_REALTIME;
	}

	//affected by NTP
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
			//TODO: Create Client socket and associate with medium
			if (hanClient != -1) {
				HanaduDeviceNode * node = new HanaduDeviceNode(hanClient);
				if (node->sendRegistrationRequest()) {
					pimpl->plcMedium->addNode(node);
				} else {
					delete node;
				}
			}
			if (airClient != -1) {
				WirelessDeviceNode * node = new WirelessDeviceNode(airClient);
				if (node->sendRegistrationRequest()) {
					pimpl->wlMedium->addNode(node);
				} else {
					delete node;
				}
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

