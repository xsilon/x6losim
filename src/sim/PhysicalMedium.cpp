#include "Simulator.hpp"
#include "socket/Socket.hpp"
#include "log/log.hpp"
#include "utils/time.hpp"

#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
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
		struct epoll_event ev;
		int rv;

		ev.events = EPOLLIN;
		ev.data.ptr = &NetworkSimulator::getUnblocker();

		state = STOPPED;
		name = strdup(nameIn);
		poller.epfd = epoll_create1(EPOLL_CLOEXEC);
		thread = -1;
		if (poller.epfd < 0)
			throw "epoll_create error";
		rv = epoll_ctl(poller.epfd, EPOLL_CTL_ADD, NetworkSimulator::getUnblocker().getReadPipe(), &ev);
		if (rv < 0)
			throw "PhysicalMedium_pimpl: failed to add unblocker to poller";
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
			throw "PhysicalMedium_pimpl: failed to create timer";
		}
	}
	~PhysicalMedium_pimpl()
	{
		close(poller.epfd);
		timer_delete(timer);
		/* Remove and free nodes from registration list and then from
		 * the node hash map, they can only be in one of these. */
		std::unordered_map<uint64_t, DeviceNode *>::iterator iter;
		regListMapMutex.lock();
		iter = regListMap.begin();
		while (iter != regListMap.end()) {
			DeviceNode *node = iter->second;

			xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from registration list",
					name, node->getNodeId());
			assert(iter->first == node->getNodeId());
			iter = regListMap.erase(iter);
			delete node;
		}
		regListMapMutex.unlock();

		std::unordered_map<uint64_t, DeviceNode *>::iterator iterMap;
		/* Remove and free nodes from the node hash map */
		iterMap = nodeHashMap.begin();
		while (iterMap != nodeHashMap.end()) {
			DeviceNode *node = iterMap->second;

			xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from node hash map",
					name, iterMap->first);
			assert(iterMap->first == node->getNodeId());
			iterMap = nodeHashMap.erase(iterMap);
			/* This will delete timer and close socket */
			delete node;
		}

		free(name);
	}

	enum PhysicalMediumState state;
	clockid_t clockidToUse;
	timer_t timer;
	struct sigevent sev;
	struct timespec curTime;
	std::unordered_map<uint64_t, DeviceNode *> nodeHashMap;
	std::unordered_map<uint64_t, DeviceNode *> regListMap;
	std::mutex regListMapMutex;
	std::list<DeviceNode *>ccaList;
	std::list<DeviceNode *>txList;
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

bool
PhysicalMedium::isIdle()
{
	return (pimpl->state == IDLE);
}

void
PhysicalMedium::addNode(DeviceNode* node)
{
	struct epoll_event ev;
	int rv;

	xlog(LOG_DEBUG, "%s: Adding Node ID (0x%016llx) to registration list",
		pimpl->name, node->getNodeId());

	node->setMedium(this);

	ev.events = EPOLLIN | EPOLLRDHUP;
	ev.data.ptr = node;

	/* epoll is thread safe so we can add the new node's socket fd to the
	 * interest list and not upset the epoll_wait that the main thread
	 * of this class may be calling.
	 */
	rv = epoll_ctl(pimpl->poller.epfd, EPOLL_CTL_ADD, node->getSocketFd(), &ev);
	if (rv < 0) {
		if (rv == EEXIST) {
			// already registered
			throw "epoll_ctl: Already registered";
		} else {
			throw "epoll_ctl: Error";
		}
	}
	pimpl->regListMapMutex.lock();
	pimpl->regListMap.insert({node->getNodeId(), node});
	pimpl->regListMapMutex.unlock();
}

void
PhysicalMedium::addNodeToCcaList(DeviceNode* node)
{
	pimpl->ccaList.push_back(node);
}

void
PhysicalMedium::addNodeToTxList(DeviceNode* node)
{
	pimpl->txList.push_back(node);
}

/*
 * Iterate through CCA list responding to the CCA requests.
 * As we are trying to model a real life physical channel we will response to
 * all nodes on this list that the channel is either free or not.  If more
 * than one node requests at the same time then we will return channel clear
 * to all these nodes who will then try and transmit and this simulator will
 * then decide that a collision has occured.
 * CCA List will be empty once function has finished.
 */
void
PhysicalMedium::processCcaList()
{
	std::list<DeviceNode *>::iterator iter;
	iter = pimpl->ccaList.begin();
	while (iter != pimpl->ccaList.end()) {
		DeviceNode *node = *iter;

		xlog(LOG_DEBUG, "%s: Processing Node ID (0x%016llx) from CCA list",
				pimpl->name, node->getNodeId());
		iter = pimpl->ccaList.erase(iter);
		node->sendCcaConfirm(pimpl->state == IDLE);
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
		/* Check for socket close first */
		if (evp->events & EPOLLRDHUP) {
			/* Closed socket */
			if (evp->data.ptr == &NetworkSimulator::getUnblocker()) {
				xlog(LOG_NOTICE, "%s: Unblocker close detected", pimpl->name);
				pimpl->state = STOPPING;
				break;
			} else {
				if (DeviceNode *node = static_cast<DeviceNode *>(evp->data.ptr)) {
					xlog(LOG_NOTICE, "Client closed (%d)", node->getSocketFd());
					/* Remove from unregList or nodeMap */
					deregisterNode(node);
					evp++;
					continue;
				} else {
					throw "Poller EPOLLRDHUP: Not a Device node";
				}
			}
		}
		if (evp->events & EPOLLIN) {
			/* Msg to read or unblocker */
			if (evp->data.ptr == &NetworkSimulator::getUnblocker()) {
				xlog(LOG_NOTICE, "%s: Unblock detected", pimpl->name);
				pimpl->state = STOPPING;
				break;
			} else {
				if (DeviceNode *node = static_cast<DeviceNode *>(evp->data.ptr)) {
					node->readMsg();
				} else {
					throw "Poller EPOLLIN: Not a Device node";
				}
			}
			xlog(LOG_NOTICE, "%s: Read detected", pimpl->name);
		}
		if (evp->events & (EPOLLERR | EPOLLHUP)) {
			/* epoll error */
		}
		evp++;
	}
}

void
PhysicalMedium::checkNodeRegistrationTimeout()
{
	std::unordered_map<uint64_t, DeviceNode *>::iterator iter;

	/* Iterate through unreg list and remove and delete all nodes that
	 * have timedout. */
	pimpl->regListMapMutex.lock();
	iter = pimpl->regListMap.begin();
	while (iter != pimpl->regListMap.end()) {
		DeviceNode *node = iter->second;

		assert(node->getNodeId() == iter->first);
		if (node->hasRegTimerExpired()) {
			xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from registration list",
					pimpl->name, node->getNodeId());
			iter = pimpl->regListMap.erase(iter);

			//This will probably delete the node so we MUST NOT
			//USE it afterwards
			node->handleRegTimerExpired();
		} else {
			// Node not registered or timed out
			iter++;
		}
	}
	pimpl->regListMapMutex.unlock();
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
				/* may set state to STOPPING */
				if (pimpl->state == STOPPING)
					break;
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

void *
PhysicalMedium::run() {

	pimpl->state = IDLE;
	if (clock_gettime(pimpl->clockidToUse, &pimpl->curTime) == -1)
		throw "clock_gettime: unrecoverable error";

	do {
		if (pimpl->state == IDLE) {
			interval(1000);
			if(pimpl->state == STOPPING)
				break;

			/* Process CCA List */
			processCcaList();



		} else if (pimpl->state == TX_802514_FRAME) {


		}
		/* Check for nodes that have expired registration period */
		checkNodeRegistrationTimeout();
	} while(pimpl->state != STOPPING);
	xlog(LOG_NOTICE, "Network Simulator Stopped");
	pimpl->state = STOPPED;
	return 0;
}

void
PhysicalMedium::stop()
{
	pimpl->state = STOPPING;
}

void
PhysicalMedium::waitForExit()
{
	void *res;
	xlog(LOG_NOTICE, "%s: Wait for exit from Physical Medium thread", pimpl->name);
	pthread_join(pimpl->thread, &res);
	xlog(LOG_NOTICE, "%s: Exit from Physical Medium thread", pimpl->name);
}

void
PhysicalMedium::registerNode(DeviceNode *node)
{
	int n;
	xlog(LOG_DEBUG, "%s: Registering node (0x%016llx) with node hash map", pimpl->name,
			node->getNodeId());

	pimpl->regListMapMutex.lock();
	n = pimpl->regListMap.erase(node->getNodeId());
	pimpl->regListMapMutex.unlock();

	assert(n == 1);
	pimpl->nodeHashMap.insert({node->getNodeId(), node});
}

void
PhysicalMedium::deregisterNode(DeviceNode* nodeToRemove)
{
	int n;
	bool nodeRemoved = false;

	/* Remove from register list map, if not found then must be on node hash map. */
	pimpl->regListMapMutex.lock();
	n = pimpl->nodeHashMap.erase(nodeToRemove->getNodeId());
	if (n == 1) {
		xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from registration list map",
				pimpl->name, nodeToRemove->getNodeId());
		delete nodeToRemove;
		nodeRemoved = true;
	}
	pimpl->regListMapMutex.unlock();
	if (n == 0) {

		n = pimpl->nodeHashMap.erase(nodeToRemove->getNodeId());
		assert (n == 1);
		xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from node hash map",
				pimpl->name, nodeToRemove->getNodeId());
		delete nodeToRemove;
		nodeRemoved = true;
	}
	assert(nodeRemoved);
}

// ______________________________________________ PowerlineMedium Implementation

void
PowerlineMedium::addNode(HanaduDeviceNode* node)
{
	PhysicalMedium::addNode(node);
}

void
PowerlineMedium::removeNode(HanaduDeviceNode* node)
{
	PhysicalMedium::deregisterNode(node);
}

// _______________________________________________ WirelessMedium Implementation

void
WirelessMedium::addNode(WirelessDeviceNode* node)
{
	PhysicalMedium::addNode(node);
}

void
WirelessMedium::removeNode(WirelessDeviceNode* node)
{
	PhysicalMedium::deregisterNode(node);
}

