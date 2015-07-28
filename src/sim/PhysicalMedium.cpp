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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pcap.h>

#include <unordered_map>
#include <list>
#include <mutex>

// _______________________________________________ PhysicalMedium Implementation

enum PhysicalMediumState {
	STOPPED,
	IDLE,
	TX_802514_FRAME,
	STOPPING
};

class PhysicalMedium_pimpl {
public:
	PhysicalMedium_pimpl(clockid_t clockidToUse, const char * nameIn, int mcastPort) : clockidToUse(clockidToUse)
	{
		struct epoll_event ev;
		int rv;
		u_char loop;

		tx.mcastsockfd = -1;
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

		// Create multicast socket and disable loopback as we don't want
		// to receive the packets we have just transmitted.
		tx.mcastsockfd=socket(AF_INET, SOCK_DGRAM, 0);
		tx.mcastport = mcastPort;
		// Ensure IP_MULTICAST_LOOP is set so QEMU nodes that are
		// running locally can receive the multicast packet.
		loop = 1;
		setsockopt(tx.mcastsockfd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

		bzero(&tx.mcastGroupAddr,sizeof(tx.mcastGroupAddr));
		tx.mcastGroupAddr.sin_family = AF_INET;
		tx.mcastGroupAddr.sin_addr.s_addr=inet_addr("224.1.1.1");
		tx.mcastGroupAddr.sin_port=htons(tx.mcastport);

		memset(&stats, 0, sizeof(stats));

		pcap.fake = pcap_open_dead(DLT_IEEE802_15_4_NOFCS, 65535);
		assert(pcap.fake);
		pcap.dumper = pcap_dump_open(pcap.fake, "./packets.pcap");
		assert(pcap.dumper);

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

		if (tx.mcastsockfd != -1)
			close(tx.mcastsockfd);

		pcap_dump_flush(pcap.dumper);
		pcap_dump_close(pcap.dumper);
		pcap_close(pcap.fake);
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
	struct {
		/* outbound multicast socket */
		int mcastsockfd;
		int mcastport;
		struct sockaddr_in mcastGroupAddr;
	} tx;
	struct {
		int tx_pkts_ok;
		int tx_pkts_collided;
		int tx_pkts_failed;
	} stats;

	struct {
		pcap_t *fake;
		pcap_dumper_t *dumper;
	} pcap;
};


PhysicalMedium::PhysicalMedium(const char * name, int port, int mcastPort, clockid_t clockidToUse)
{
	pimpl = new PhysicalMedium_pimpl(clockidToUse, name, mcastPort);

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

/* This list basically indicates what node(s) is/are transmitting on the
 * medium.   It is up to the sub class of PhysicalMedium to make the decision
 * as to whether there are collisions or not. */
void
PhysicalMedium::addNodeToTxList(DeviceNode* node)
{
	assert(node);
	xlog(LOG_DEBUG, "%s: Add Node ID (0x%016llx) to Tx list",
			pimpl->name, node->getNodeId());
	pimpl->txList.push_back(node);
}

void
PhysicalMedium::removeNodeFromTxList(DeviceNode *nodeIn)
{
	bool found = false;

	std::list<DeviceNode *>::iterator iter;
	iter = pimpl->txList.begin();
	while (iter != pimpl->txList.end()) {
		DeviceNode *node = *iter;

		if (node == nodeIn) {
			iter = pimpl->txList.erase(iter);
			found = true;
		}
	}

	assert(pimpl->state == TX_802514_FRAME);
	assert(found);
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
				xlog(LOG_NOTICE, "%s: Read detected", pimpl->name);
				if (DeviceNode *node = static_cast<DeviceNode *>(evp->data.ptr)) {
					node->readMsg();
				} else {
					throw "Poller EPOLLIN: Not a Device node";
				}
			}
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

void
PhysicalMedium::txPacket(DeviceNode *node, NetSimPacket * pkt)
{
	struct pcap_pkthdr pcaphdr;
	// Check to see if there is a packet to transmit
	assert(node);
	//TODO: Fill in RSSI, for now set it to 127
	pkt->setRSSI(127);
	pkt->generateChecksum();
	sendto(pimpl->tx.mcastsockfd, pkt->buf(), pkt->bufSize(), 0,
		(struct sockaddr *)&pimpl->tx.mcastGroupAddr,
		sizeof(pimpl->tx.mcastGroupAddr));

	xlog(LOG_DEBUG, "%s: Node (0x%016llx) Tx packet Len %d",
			pimpl->name, node, pkt->pktbufSize());
	xlog_hexdump(LOG_DEBUG, pkt->pktbuf(), pkt->pktbufSize());

	gettimeofday(&pcaphdr.ts,NULL);
	pcaphdr.len = pkt->pktbufSize();
	pcaphdr.caplen = pkt->pktbufSize();;
	pcap_dump((u_char *)pimpl->pcap.dumper, &pcaphdr, pkt->pktbuf());
}

void
PhysicalMedium::processTxList()
{
	std::list<DeviceNode *>::iterator iter;

	/* Iterate through unreg list and remove and delete all nodes that
	 * have timedout. */
	iter = pimpl->txList.begin();
	while (iter != pimpl->txList.end()) {
		DeviceNode *node = *iter;

		if (node->hasTxTimerExpired()) {
			NetSimPacket * pkt = node->getTxPacket();
			int txDoneResult;

			assert(pkt);
			// This will set Tx Done Result
			node->handleTxTimerExpired();
			txDoneResult = pkt->getTxDoneResult();
			node->sendTxDoneIndication(txDoneResult);
			xlog(LOG_DEBUG, "%s: Node (0x%016llx) Tx Done Ind(%d)",
					pimpl->name, node, txDoneResult);

			if (txDoneResult == TX_DONE_OK) {
				xlog(LOG_DEBUG, "%s: Node (0x%016llx) Tx packet",
						pimpl->name, node);
				txPacket(node, pkt);
				pimpl->stats.tx_pkts_ok++;
			} else if (txDoneResult == TX_DONE_COLLIDED) {
				pimpl->stats.tx_pkts_collided++;
			} else if (txDoneResult == TX_DONE_FAILURE) {
				pimpl->stats.tx_pkts_failed++;
			} else {
				throw "Invalid TX Done Result";
			}
			xlog(LOG_DEBUG, "%s: ok(%d) collided(%d) failed(%d)",
					pimpl->name, pimpl->stats.tx_pkts_ok,
					pimpl->stats.tx_pkts_collided,
					pimpl->stats.tx_pkts_failed);
			// Delete and Clear transmitted packet
			node->setTxPacket(NULL);

			xlog(LOG_DEBUG, "%s: Removing Node ID (0x%016llx) from TX list",
					pimpl->name, node->getNodeId());
			iter = pimpl->txList.erase(iter);
		} else {
			// Node not registered or timed out
			iter++;
		}
	}
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

			if (!pimpl->txList.empty())
				pimpl->state = TX_802514_FRAME;

			// Process CCA List
			processCcaList();
		}

		// In this loop we wait until the Tx list is empty. During this
		// time if we get more than 1 node in the Tx List we have a
		// collision.
		while (!pimpl->txList.empty()) {

			// TODO: Maybe use timerfd to get finer resolution with
			// Tx Packet Timings.
			interval(1);
			if(pimpl->state == STOPPING)
				break;

			// Process CCA List, as we are in state TX_802514_FRAME
			// This routine will tell nodes they have failed CCA.
			processCcaList();

			// Check with subclasses collision check, this needs to
			// be done before we check timeouts as if a tx timer
			// expires it will check for collisions and arm the
			// next packet to tx member variable.
			txCollisionCheck();

			// Go through Tx list and process timers and if one has
			// expired send the Tx Done Indication with the result
			// of the transmission.
			processTxList();

		}
		if(pimpl->state != STOPPING) {
			/* Check for nodes that have expired registration period */
			pimpl->state = IDLE;
			checkNodeRegistrationTimeout();
		}
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
	n = pimpl->regListMap.erase(nodeToRemove->getNodeId());
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

void
PowerlineMedium::txCollisionCheck()
{
	std::list<DeviceNode *>::iterator iter;

	if (pimpl->txList.size() > 1) {
		iter = pimpl->txList.begin();
		while (iter != pimpl->txList.end()) {
			DeviceNode *node = *iter;

			node->getTxPacket()->setCollided(true);
			iter++;
		}
	}
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

void
WirelessMedium::txCollisionCheck()
{

}
