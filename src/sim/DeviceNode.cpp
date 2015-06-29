#include "Simulator.hpp"
#include "socket/Socket.hpp"
#include "log/log.hpp"
#include "utils/compiler.h"

#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>


/*
 *  Our algorithm is simple, using a 32 bit accumulator (sum),
 *  we add sequential 16 bit words to it, and at the end, fold
 *  back all the carry bits from the top 16 bits into the lower
 *  16 bits.
 */
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

#ifndef ntohll
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntohll(uint64_t x) {
	return bswap_64(x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t ntohll(uint64_t x)
{
	return x;
}
#endif
#endif
#ifndef htonll
#define htonll ntohll
#endif

NetSimPacket::NetSimPacket(node_to_netsim_data_ind_pkt *dataInd, DeviceNode *fromNode) :
	fromNode(fromNode)
{
	BUILD_BUG_ON(sizeof(pktBuffer) != sizeof(*dataInd));
	memcpy(pktBuffer, dataInd, sizeof(pktBuffer));
}


class IDeviceNodeState
{
public:
	virtual ~IDeviceNodeState() {}
	virtual void enter(DeviceNode &node) { }
	virtual void exit(DeviceNode &node) { }
	virtual IDeviceNodeState *handleRegTimerExpired(DeviceNode &node) { return NULL; }
	virtual IDeviceNodeState *handleTxTimerExpired(DeviceNode &node) { return NULL; }
	virtual IDeviceNodeState *handleRegistrationConfirm(DeviceNode &node) {  return NULL; }
	virtual IDeviceNodeState *handleDeregistrationRequest(DeviceNode &node) {  return NULL; }
	virtual IDeviceNodeState *handleCcaRequest(DeviceNode &node) { return NULL; }
	virtual IDeviceNodeState *handleTxRequest(DeviceNode &node, NetSimPacket &dataPkt) { return NULL; }
};

class RegisteringState : public IDeviceNodeState
{
public:
	RegisteringState(DeviceNode &node)
	{
		node.sendRegistrationRequest();
		/* Start registration timer */
		node.startRegistrationTimer();
	}
	IDeviceNodeState *handleRegTimerExpired(DeviceNode &node)
	{
		delete(&node);
		return NULL;
	}
	IDeviceNodeState *handleTxTimerExpired(DeviceNode &node)
	{
		throw "RegisteringState: handleTxTimerExpired";
	}
	IDeviceNodeState *handleRegistrationConfirm(DeviceNode &node);

	IDeviceNodeState *handleDeregistrationRequest(DeviceNode &node)
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node.getMedium()->deregisterNode(&node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest(DeviceNode &node)
	{
		throw "RegisteringState: handleCcaRequest";
	}
	IDeviceNodeState *handleTxRequest(DeviceNode &node, NetSimPacket &dataPkt)
	{
		throw "RegisteringState: handleTxRequest";
	}
};


class ActiveState : public IDeviceNodeState
{
public:
	ActiveState() {}
	IDeviceNodeState *handleRegTimerExpired(DeviceNode &node)
	{
		throw "ActiveState: handleCheckRegTimer";
	}
	IDeviceNodeState *handleTxTimerExpired(DeviceNode &node) { return NULL; }
	IDeviceNodeState *handleRegistrationConfirm(DeviceNode &node)
	{
		throw "ActiveState: handleRegistrationConfirm";
	}
	IDeviceNodeState *handleDeregistrationRequest(DeviceNode &node)
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node.getMedium()->deregisterNode(&node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest(DeviceNode &node)
	{
		// This is where we expect to get a CCA request
		return NULL;
	}
	IDeviceNodeState *handleTxRequest(DeviceNode &node, NetSimPacket &dataPkt);
};

class TxState : public IDeviceNodeState
{
	IDeviceNodeState *handleRegTimerExpired(DeviceNode &node)
	{
		throw "TxState: handleCheckRegTimer";
	}
	IDeviceNodeState *handleTxTimerExpired(DeviceNode &node)
	{
		node.stopTxTimer();
		// TODO: If there were no collisions transmit packet

		// TODO: Remove node from list
		// Clear transmitted packet
		node.setTxPacket(NULL);
		return new ActiveState();
	}
	IDeviceNodeState *handleRegistrationConfirm(DeviceNode &node)
	{
		throw "TxState: handleRegistrationConfirm";
	}
	IDeviceNodeState *handleDeregistrationRequest(DeviceNode &node)
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node.getMedium()->deregisterNode(&node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest(DeviceNode &node)
	{
		throw "TxState: handleCcaRequest";
	}
	IDeviceNodeState *handleTxRequest(DeviceNode &node, NetSimPacket &dataPkt)
	{
		throw "TxState: handleCcaRequest";
	}
};

// Implementation of State functions that can't be contained in the actual class

IDeviceNodeState *RegisteringState::handleRegistrationConfirm(DeviceNode &node)
{
	node.stopRegistrationTimer();
	node.getMedium()->registerNode(&node);

	return new ActiveState();
}

IDeviceNodeState *ActiveState::handleTxRequest(DeviceNode &node, NetSimPacket &dataPkt)
{
	//Should be in Tx State
	node.startTxTimer(dataPkt.getTimeOnWire());
	node.setTxPacket(&dataPkt);
	node.getMedium()->addNodeToTxList(&node);
	return new TxState();
}

// ___________________________________________________ DeviceNode Implementation

class DeviceNodeTimer {
public:
	DeviceNodeTimer(int iSec, int iNSec, int tSec, int tNSec)
	{
		timerSpec.it_interval.tv_sec = iSec;
		timerSpec.it_interval.tv_nsec = iNSec;
		timerSpec.it_value.tv_sec = tSec;
		timerSpec.it_value.tv_nsec = tNSec;
		memset(&sigEvent, 0, sizeof(sigEvent));
		sigEvent.sigev_notify = SIGEV_NONE;
		sigEvent.sigev_signo = SIGSTOP;
		timer = NULL;
		started = false;
	};

	void
	start()
	{
		assert(!started);
		if (timer_create(NetworkSimulator::getClockId(),
				 &sigEvent, &timer) == -1)
			throw "DeviceNodeTimer::start: failed to create timer";
		if (timer_settime(timer, 0, &timerSpec, NULL) == -1)
			throw "DeviceNodeTimer::start: failed to arm timer";

		started = true;
	}
	void
	start(int ms)
	{
		assert(!started);

		timerSpec.it_value.tv_sec = ms /1000;
		timerSpec.it_value.tv_nsec = (ms % 1000) * 1000000;

		if (timer_create(NetworkSimulator::getClockId(),
				 &sigEvent, &timer) == -1)
			throw "DeviceNodeTimer::start: failed to create timer";
		if (timer_settime(timer, 0, &timerSpec, NULL) == -1)
			throw "DeviceNodeTimer::start: failed to arm timer";

		started = true;
	}

	void
	stop()
	{
		if (started) {
			timer_delete(timer);
			started = false;
		}
	}

	bool
	expired()
	{
		itimerspec ts_left;

		assert (started);

		if (timer_gettime(timer, &ts_left) == -1)
			throw "DeviceNodeTimer: failed to get time";

		if (ts_left.it_value.tv_sec == 0 && ts_left.it_value.tv_nsec == 0)
			return true;
		else
			return false;

	}

	struct sigevent sigEvent;
	timer_t timer;
	bool started;
	struct itimerspec timerSpec;
};

class DeviceNode_pimpl {
public:
	DeviceNode_pimpl(int sockfd) : sockfd(sockfd)
	{
		socket = new Socket(sockfd);
		/*
		 * SIGEV_NONE is supposed to prevent signal delivery, but it doesn't.
		 * Set signo to SIGSTOP to make the received signal obvious but
		 * harmless.
		 */
		memset(&stats, 0, sizeof(stats));
		memset(&os, 0, sizeof(os));
		memset(&osVersion, 0, sizeof(osVersion));
		medium = NULL;
		curState = NULL;
		txPkt = NULL;
	}
	~DeviceNode_pimpl()
	{
		delete socket;
		delete curState;
	}

	PhysicalMedium *medium;
	int sockfd;
	Socket * socket;
	char os[32];
	char osVersion[32];
	IDeviceNodeState *curState;

	static DeviceNodeTimer regTimer;
	static DeviceNodeTimer txTimer;

	NetSimPacket *txPkt;

	struct stats {
		int failedReads;
	} stats;
};

DeviceNodeTimer DeviceNode_pimpl::regTimer(0, 0, REGISTRATION_TIME, 0);
DeviceNodeTimer DeviceNode_pimpl::txTimer(0, 0, 0, 0);

DeviceNode::DeviceNode(int sockfd)
{
	pimpl = new DeviceNode_pimpl(sockfd);
	pimpl->socket->setCloseOnExec(true);
	//Put this node in registering state which will send registration req.
	pimpl->curState = new RegisteringState(*this);


}

DeviceNode::~DeviceNode()
{
	xlog(LOG_DEBUG, "Destroying node (0x%016llx)", getNodeId());
	//This is safe to call even if timer hasn't been started.
	stopRegistrationTimer();
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

bool
DeviceNode::hasRegTimerExpired()
{
	return pimpl->regTimer.expired();
}

void
DeviceNode::sendRegistrationRequest()
{
	struct netsim_to_node_registration_req_pkt *msg;
	int msglen = sizeof(*msg);
	int n;

	msg = (netsim_to_node_registration_req_pkt *)malloc(msglen);

	msg->hdr.len = htons(sizeof(*msg));
	msg->hdr.msg_type = htons(MSG_TYPE_REG_REQ);
	msg->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = htonll((uint64_t)pimpl->socket);
	msg->hdr.cksum = htons(generate_checksum(msg, msglen));

	/* Send registration request message, on return the caller will add
	 * to the relevant physical medium which will then process the
	 * confirm or remove if not received within a period of time. */
	n = pimpl->socket->sendMsg((char *)msg, msglen);
	assert(n == msglen);

	free(msg);
}

void
DeviceNode::sendDeregistrationConfirm()
{
	struct netsim_to_node_deregistration_con_pkt *msg;
	int msglen = sizeof(*msg);

	msg = (netsim_to_node_deregistration_con_pkt *)malloc(msglen);

	msg->hdr.len = htons(sizeof(*msg));
	msg->hdr.msg_type = htons(MSG_TYPE_DEREG_CON);
	msg->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = htonll((uint64_t)pimpl->socket);
	msg->hdr.cksum = htons(generate_checksum(msg, msglen));

	pimpl->socket->sendMsg((char *)msg, msglen);

	free(msg);
}

void
DeviceNode::sendCcaConfirm(bool result)
{
	struct netsim_to_node_cca_con_pkt *msg;
	int msglen = sizeof(*msg);

	msg = (netsim_to_node_cca_con_pkt *)malloc(msglen);

	msg->hdr.len = htons(sizeof(*msg));
	msg->hdr.msg_type = htons(MSG_TYPE_DEREG_CON);
	msg->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = htonll((uint64_t)pimpl->socket);
	msg->hdr.cksum = htons(generate_checksum(msg, msglen));
	msg->result = result ? 1 : 0;

	pimpl->socket->sendMsg((char *)msg, msglen);

	free(msg);
}

void
DeviceNode::readMsg()
{
	char *msgData = NULL;
	int rv;
	uint16_t len;

	/* The PhysicalMedium will only ask a device node to read the message
	 * out if the poller has detected that data is available so we can
	 * safely call recv here.  The socket is non blocking anyway so we
	 * can check for EAGAIN which in theory shouldn't happen unless
	 * we change epoll to edge triggered.
	 */
	rv = recv(pimpl->socket->getSockFd(), &len, sizeof(len), 0);
	if (rv == EAGAIN) {
		// shouldn't happen, just log and return.
		xlog(LOG_WARNING, "DeviceNode::readMsg: EAGAIN");
		pimpl->stats.failedReads++;
	} else {
		uint16_t msgType;
		uint32_t interfaceVersion;
		uint64_t nodeId;
		struct netsim_pkt_hdr *hdr;

		assert(rv == sizeof(len));
		assert(len >= sizeof(hdr));

		len = ntohs(len);
		msgData = (char *)malloc(len);
		rv = recv(pimpl->socket->getSockFd(), msgData+sizeof(len), len-sizeof(len), 0);
		if (rv == EAGAIN) {
			// shouldn't happen, just log and return.
			xlog(LOG_WARNING, "DeviceNode::readMsg: EAGAIN");
			pimpl->stats.failedReads++;
			goto cleanup;
		}
		hdr = (struct netsim_pkt_hdr *)msgData;
		msgType = ntohs(hdr->msg_type);
		interfaceVersion = ntohl(hdr->interface_version);
		nodeId = ntohll(hdr->node_id);

		assert(nodeId == (uint64_t)pimpl->socket);

		xlog(LOG_INFO, "Msg Len  : %u\n", len);
		xlog(LOG_INFO, "Msg Type : %u\n", msgType);
		xlog(LOG_INFO, "Interface: 0x%08x\n", interfaceVersion);
		xlog(LOG_INFO, "Node ID  : 0x%016llx\n",(long long unsigned int) nodeId);

		if (interfaceVersion != NETSIM_INTERFACE_VERSION) {
			xlog(LOG_WARNING, "Invalid Interface version 0x%08x != 0x%08x(received)\n",
					NETSIM_INTERFACE_VERSION, interfaceVersion);
			goto cleanup;
		}

		// TODO: Check checksum
		switch(msgType) {
		case MSG_TYPE_REG_CON:
			xlog(LOG_INFO, "MSG_TYPE_REG_CON");
			handleRegistrationConfirm((node_to_netsim_registration_con_pkt *)msgData);
			break;
		case MSG_TYPE_DEREG_REQ:
			xlog(LOG_INFO, "MSG_TYPE_DEREG_REQ");
			handleDeregistrationRequest((node_to_netsim_deregistration_req_pkt *)msgData);
			//The node will have been deleted so make sure we don't
			//try and use any of the node's data.
			break;
		case MSG_TYPE_CCA_REQ:
			xlog(LOG_INFO, "MSG_TYPE_CCA_REQ");
			handleCcaRequest((node_to_netsim_cca_req_pkt *)msgData);
			break;
		case MSG_TYPE_TX_DATA_IND:
			xlog(LOG_INFO, "MSG_TYPE_CCA_REQ");
			handleDataIndication((node_to_netsim_data_ind_pkt *)msgData);
			break;
		// These aren't supported
		case MSG_TYPE_REG_REQ:
		case MSG_TYPE_CCA_CON:
		default:
			//Bad msg type
			xlog(LOG_ERR, "DeviceNode::readMsg: Invalid msgType");
		}
	}
cleanup:
	if (msgData)
		free(msgData);

}

void
DeviceNode::startRegistrationTimer()
{
	pimpl->regTimer.start();
}

void
DeviceNode::stopRegistrationTimer()
{
	pimpl->regTimer.stop();
}

void
DeviceNode::startTxTimer(int msTimeout)
{
	pimpl->txTimer.start(msTimeout);
}

void
DeviceNode::stopTxTimer()
{
	pimpl->txTimer.stop();
}


PhysicalMedium *
DeviceNode::getMedium()
{
	return pimpl->medium;
}

void
DeviceNode::setMedium(PhysicalMedium *medium) {
	pimpl->medium = medium;
}

/* Use setTxPacket(NULL) to clear transmitted packet */
void
DeviceNode::setTxPacket(NetSimPacket *pkt)
{
	assert(pimpl->txPkt == NULL);
	pimpl->txPkt = pkt;
}

// ______________________________________________ DeviceNodeState Implementation

void
DeviceNode::handleRegTimerExpired()
{
	IDeviceNodeState *newState = pimpl->curState->handleRegTimerExpired(*this);
	if (newState) {
		pimpl->curState->exit(*this);
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter(*this);
	}
}

void
DeviceNode::handleRegistrationConfirm(node_to_netsim_registration_con_pkt *regCon)
{
	IDeviceNodeState *newState;

	BUILD_BUG_ON(sizeof(pimpl->os) != sizeof(regCon->os));
	BUILD_BUG_ON(sizeof(pimpl->osVersion) != sizeof(regCon->os_version));

	strncpy(pimpl->os, regCon->os, sizeof(pimpl->os));
	strncpy(pimpl->osVersion, regCon->os, sizeof(pimpl->osVersion));

	newState = pimpl->curState->handleRegistrationConfirm(*this);
	if (newState) {
		pimpl->curState->exit(*this);
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter(*this);
	}
	//TODO: Assert pimpl->state = DEV_NODE_STATE_ACTIVE;
}

void
DeviceNode::handleDeregistrationRequest(node_to_netsim_deregistration_req_pkt *deregReq)
{
	IDeviceNodeState *newState;

	// No need to check and stop timer as we will be destroying node
	// which will handle this.
	sendDeregistrationConfirm();

	newState = pimpl->curState->handleDeregistrationRequest(*this);
	if (newState) {
		pimpl->curState->exit(*this);
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter(*this);
	}
}

void
DeviceNode::handleCcaRequest(node_to_netsim_cca_req_pkt *ccaReq)
{
	IDeviceNodeState *newState;

	/* Medium will process the CCA list and respond to this request as the
	 * Medium class is responsible for deciding who gets access. */
	pimpl->medium->addNodeToCcaList(this);

	newState = pimpl->curState->handleCcaRequest(*this);
	if (newState) {
		pimpl->curState->exit(*this);
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter(*this);
	}
}

void
DeviceNode::handleDataIndication(node_to_netsim_data_ind_pkt *dataInd)
{
	IDeviceNodeState *newState;
	NetSimPacket * dataPkt;

	dataPkt = new NetSimPacket(dataInd, this);
	newState = pimpl->curState->handleTxRequest(*this, *dataPkt);
	if (newState) {
		pimpl->curState->exit(*this);
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter(*this);
	}
}

// _____________________________________________ HanaduDeviceNode Implementation

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

// ___________________________________________ WirelessDeviceNode Implementation

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
