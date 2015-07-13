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

NetSimPacket::NetSimPacket(netsim_data_ind_pkt *dataInd, DeviceNode *fromNode) :
	fromNode(fromNode)
{
	pktBufferLen = ntohs(dataInd->psdu_len) + sizeof(*dataInd) - 1;

	pktBuffer = (uint8_t *)malloc(pktBufferLen);
	memcpy(pktBuffer, dataInd, pktBufferLen);
	collision = false;
	txDoneResult = false;
}

NetSimPacket::~NetSimPacket()
{
	free(pktBuffer);
}

void
NetSimPacket::generateChecksum()
{
	uint16_t cksum;

	cksum = generate_checksum(pktBuffer, pktBufferLen);
	((netsim_data_ind_pkt *)pktBuffer)->hdr.cksum = htons(cksum);
}


class IDeviceNodeState
{
public:
	IDeviceNodeState(DeviceNode *node)
	{
		this->node = node;
	}
	virtual ~IDeviceNodeState() {}
	virtual void enter() { }
	virtual void exit() { }
	virtual IDeviceNodeState *handleRegTimerExpired() { return NULL; }
	virtual IDeviceNodeState *handleTxTimerExpired() { return NULL; }
	virtual IDeviceNodeState *handleRegistrationConfirm() {  return NULL; }
	virtual IDeviceNodeState *handleDeregistrationRequest() {  return NULL; }
	virtual IDeviceNodeState *handleCcaRequest() { return NULL; }
	virtual IDeviceNodeState *handleTxRequest(NetSimPacket &dataPkt) { return NULL; }
protected:
	DeviceNode *node;
};

class RegisteringState : public IDeviceNodeState
{
public:
	RegisteringState(DeviceNode *node) : IDeviceNodeState(node)
	{
		node->sendRegistrationRequest();
		// Start registration timer
		node->startRegistrationTimer();
	}
	IDeviceNodeState *handleRegTimerExpired()
	{
		// The PhysicalMedium will have removed the node from the
		// relevant data structures so all that's left to do is free
		// up the node's memory.
		delete(node);
		return NULL;
	}
	IDeviceNodeState *handleTxTimerExpired()
	{
		throw "RegisteringState: handleTxTimerExpired";
	}
	IDeviceNodeState *handleRegistrationConfirm();

	IDeviceNodeState *handleDeregistrationRequest()
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node->getMedium()->deregisterNode(node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest()
	{
		throw "RegisteringState: handleCcaRequest";
	}
	IDeviceNodeState *handleTxRequest(NetSimPacket &dataPkt)
	{
		throw "RegisteringState: handleTxRequest";
	}
};


class ActiveState : public IDeviceNodeState
{
public:
	ActiveState(DeviceNode *node) : IDeviceNodeState(node) {}
	IDeviceNodeState *handleRegTimerExpired()
	{
		throw "ActiveState: handleCheckRegTimer";
	}
	IDeviceNodeState *handleTxTimerExpired()
	{
		throw "ActiveState: handleTxTimerExpired";
	}
	IDeviceNodeState *handleRegistrationConfirm()
	{
		throw "ActiveState: handleRegistrationConfirm";
	}
	IDeviceNodeState *handleDeregistrationRequest()
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node->getMedium()->deregisterNode(node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest()
	{
		// This is where we expect to get a CCA request
		return NULL;
	}
	IDeviceNodeState *handleTxRequest(NetSimPacket &dataPkt);
};

class TxState : public IDeviceNodeState
{
public:
	TxState(DeviceNode *node) : IDeviceNodeState(node) {}
	IDeviceNodeState *handleRegTimerExpired()
	{
		throw "TxState: handleCheckRegTimer";
	}
	IDeviceNodeState *handleTxTimerExpired()
	{
		NetSimPacket *pkt = node->getTxPacket();
		assert(pkt);
		pkt->setTxDoneResult(TX_DONE_OK);

		node->stopTxTimer();
		// TODO: Need to check if simulator has decided packet is to fail.
		if (!pkt->hasCollided()) {
			node->getMedium()->setPktForTransmission(pkt);
		} else {
			pkt->setTxDoneResult(TX_DONE_COLLIDED);
		}

		// The physical medium will remove the node from the Tx List,
		// Transmit the packet using the multicast socket and then
		// Send back the Tx Done Indication to the node to inform
		// it that the packet has been sent (or has collided).

		// Clear transmitted packet
		node->setTxPacket(NULL);
		return new ActiveState(node);
	}
	IDeviceNodeState *handleRegistrationConfirm()
	{
		throw "TxState: handleRegistrationConfirm";
	}
	IDeviceNodeState *handleDeregistrationRequest()
	{
		//Deregister node (this will delete it to so ensure we return
		//NULL to stop caller trying to use the deleted node
		node->getMedium()->deregisterNode(node);
		return NULL;
	}
	IDeviceNodeState *handleCcaRequest(DeviceNode &node)
	{
		throw "TxState: handleCcaRequest";
	}
	IDeviceNodeState *handleTxRequest(NetSimPacket &dataPkt)
	{
		throw "TxState: handleCcaRequest";
	}
};

// Implementation of State functions that can't be contained in the actual class

IDeviceNodeState *RegisteringState::handleRegistrationConfirm()
{
	node->stopRegistrationTimer();
	node->getMedium()->registerNode(node);

	return new ActiveState(node);
}

IDeviceNodeState *ActiveState::handleTxRequest(NetSimPacket &dataPkt)
{
	//Should be in Tx State
	node->startTxTimer(dataPkt.getTimeOnWire());
	node->setTxPacket(&dataPkt);
	node->getMedium()->addNodeToTxList(node);
	return new TxState(node);
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
	DeviceNode_pimpl(int sockfd) : sockfd(sockfd),
				       regTimer(0, 0, REGISTRATION_TIME, 0),
				       txTimer(0, 0, 0, 0)
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

	DeviceNodeTimer regTimer;
	DeviceNodeTimer txTimer;

	NetSimPacket *txPkt;

	struct stats {
		int failedReads;
	} stats;
};

DeviceNode::DeviceNode(int sockfd)
{
	pimpl = new DeviceNode_pimpl(sockfd);
	pimpl->socket->setCloseOnExec(true);
	//Put this node in registering state which will send registration req.
	pimpl->curState = new RegisteringState(this);


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

bool
DeviceNode::hasTxTimerExpired()
{
	return pimpl->txTimer.expired();
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
	/* We must blank out the checksum field before running checksum calc. */
	msg->hdr.cksum = 0;
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
	/* We must blank out the checksum field before running checksum calc. */
	msg->hdr.cksum = 0;
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
	msg->hdr.msg_type = htons(MSG_TYPE_CCA_CON);
	msg->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = htonll((uint64_t)pimpl->socket);
	msg->result = result ? 1 : 0;

	/* We must blank out the checksum field before running checksum calc. */
	msg->hdr.cksum = 0;
	msg->hdr.cksum = htons(generate_checksum(msg, msglen));

	pimpl->socket->sendMsg((char *)msg, msglen);

	free(msg);
}

void
DeviceNode::sendTxDoneIndication(int result)
{
	struct netsim_to_node_tx_done_ind_pkt *msg;
	int msglen = sizeof(*msg);

	msg = (netsim_to_node_tx_done_ind_pkt *)malloc(msglen);

	msg->hdr.len = htons(sizeof(*msg));
	msg->hdr.msg_type = htons(MSG_TYPE_TX_DONE_IND);
	msg->hdr.interface_version = htonl(NETSIM_INTERFACE_VERSION);
	/* We'll use the virtual address of the socket instance pointer as this
	 * is always guaranteed to be unique for each DeviceNode instance. */
	assert(pimpl->socket != NULL);
	msg->hdr.node_id = htonll((uint64_t)pimpl->socket);
	msg->result = (uint8_t)result;

	/* We must blank out the checksum field before running checksum calc. */
	msg->hdr.cksum = 0;
	msg->hdr.cksum = htons(generate_checksum(msg, msglen));

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
		uint16_t rxCksum, calculatedCksum;
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
		// Check checksum, need to reset cksum to 0 and fill in message
		// length first.
		rxCksum = ntohs(hdr->cksum);
		hdr->cksum = 0;
		hdr->len = htons(len);
		calculatedCksum = generate_checksum(msgData, len);
		if (calculatedCksum != rxCksum) {
			xlog(LOG_WARNING, "Invalid Checksum (0x%04x != 0x%04x)",
				calculatedCksum, rxCksum);
			goto cleanup;
		}

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
			xlog(LOG_INFO, "MSG_TYPE_TX_DATA_IND");
			/* Checksum is regenerated later on before we actually
			   transmit it. */
			handleDataIndication((netsim_data_ind_pkt *)msgData);
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

NetSimPacket *
DeviceNode::getTxPacket()
{
	return pimpl->txPkt;
}

/* Use setTxPacket(NULL) to clear transmitted packet */
void
DeviceNode::setTxPacket(NetSimPacket *pkt)
{
	pimpl->txPkt = pkt;
}

// ______________________________________________ DeviceNodeState Implementation

void
DeviceNode::handleRegTimerExpired()
{
	IDeviceNodeState *newState = pimpl->curState->handleRegTimerExpired();
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
	}
}

void
DeviceNode::handleTxTimerExpired()
{
	IDeviceNodeState *newState = pimpl->curState->handleTxTimerExpired();
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
	}
}

void
DeviceNode::handleRegistrationConfirm(node_to_netsim_registration_con_pkt *regCon)
{
	IDeviceNodeState *newState;

	BUILD_BUG_ON(sizeof(pimpl->os) != sizeof(regCon->os));
	BUILD_BUG_ON(sizeof(pimpl->osVersion) != sizeof(regCon->os_version));

	strncpy(pimpl->os, regCon->os, sizeof(pimpl->os));
	strncpy(pimpl->osVersion, regCon->os_version, sizeof(pimpl->osVersion));
	xlog(LOG_DEBUG, "Reg Confirm: OS:%s V:%s", pimpl->os, pimpl->osVersion);

	newState = pimpl->curState->handleRegistrationConfirm();
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
	}
}

void
DeviceNode::handleDeregistrationRequest(node_to_netsim_deregistration_req_pkt *deregReq)
{
	IDeviceNodeState *newState;

	// No need to check and stop timer as we will be destroying node
	// which will handle this.
	sendDeregistrationConfirm();

	newState = pimpl->curState->handleDeregistrationRequest();
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
	}
}

void
DeviceNode::handleCcaRequest(node_to_netsim_cca_req_pkt *ccaReq)
{
	IDeviceNodeState *newState;

	/* Medium will process the CCA list and respond to this request as the
	 * Medium class is responsible for deciding who gets access. */
	pimpl->medium->addNodeToCcaList(this);

	newState = pimpl->curState->handleCcaRequest();
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
	}
}

void
DeviceNode::handleDataIndication(netsim_data_ind_pkt *dataInd)
{
	IDeviceNodeState *newState;
	NetSimPacket * dataPkt;

	dataPkt = new NetSimPacket(dataInd, this);
	newState = pimpl->curState->handleTxRequest(*dataPkt);
	if (newState) {
		pimpl->curState->exit();
		delete pimpl->curState;
		pimpl->curState = newState;
		pimpl->curState->enter();
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
