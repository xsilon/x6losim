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
		memset(&regTimerSigEvent, 0, sizeof(regTimerSigEvent));
		regTimerSigEvent.sigev_notify = SIGEV_NONE;
		regTimerSigEvent.sigev_signo = SIGSTOP;
		regTimer = NULL;
		memset(&stats, 0, sizeof(stats));
		memset(&os, 0, sizeof(os));
		memset(&osVersion, 0, sizeof(osVersion));
		regTimerStarted = false;
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
	bool regTimerStarted;

	struct stats {
		int failedReads;
	} stats;
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


DeviceNode::DeviceNode(int sockfd)
{
	pimpl = new DeviceNode_pimpl(sockfd);
	pimpl->socket->setCloseOnExec(true);

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

DeviceNodeState
DeviceNode::getState()
{
	return pimpl->state;
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

	/* Start registration timer */
	pimpl->state = DEV_NODE_STATE_REGISTERING;
	startRegistrationTimer();

	free(msg);
}

void
DeviceNode::sendDeregistrationConfirm()
{
	struct node_to_netsim_deregistration_req_pkt *msg;
	int msglen = sizeof(*msg);

	msg = (node_to_netsim_deregistration_req_pkt *)malloc(msglen);

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
DeviceNode::handleRegistrationConfirm(node_to_netsim_registration_con_pkt *regCon)
{
	BUILD_BUG_ON(sizeof(pimpl->os) != sizeof(regCon->os));
	BUILD_BUG_ON(sizeof(pimpl->osVersion) != sizeof(regCon->os_version));

	stopRegistrationTimer();
	strncpy(pimpl->os, regCon->os, sizeof(pimpl->os));
	strncpy(pimpl->osVersion, regCon->os, sizeof(pimpl->osVersion));
	pimpl->state = DEV_NODE_STATE_ACTIVE;
}

void
DeviceNode::handleDeregistrationRequest(node_to_netsim_deregistration_req_pkt *deregReq)
{
	// No need to check and stop timer as we will be destroying node
	// which will handle this.
	pimpl->state = DEV_NODE_STATE_DEREGISTERING;
	sendDeregistrationConfirm();
}

void
DeviceNode::readMsg(PhysicalMedium *medium)
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
			if (pimpl->state == DEV_NODE_STATE_REGISTERING) {
				handleRegistrationConfirm((node_to_netsim_registration_con_pkt *)msgData);
				medium->registerNode(this);
			} else {
				xlog(LOG_ERR, "Received reg confirm for a node that is in state %d", pimpl->state);
			}
			break;
		case MSG_TYPE_DEREG_REQ:
			xlog(LOG_INFO, "MSG_TYPE_DEREG_REQ");
			if (pimpl->state == DEV_NODE_STATE_REGISTERING
				|| pimpl->state == DEV_NODE_STATE_ACTIVE
				|| pimpl->state == DEV_NODE_STATE_TX
			) {
				//TODO: Do we need to do something special if we
				//are in TX state.
				handleDeregistrationRequest((node_to_netsim_deregistration_req_pkt *)msgData);
				//The medium has to delete this node so we don't deregister here,
				//The caller will check the state and do this on return.
			} else {
				xlog(LOG_ERR, "Received reg confirm for a node that is in state %d", pimpl->state);
			}
			break;
			break;
		case MSG_TYPE_CCA_REQ:
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
	if (timer_create(NetworkSimulator::getClockId(),
			 &pimpl->regTimerSigEvent,
			 &pimpl->regTimer) == -1)
		throw "DeviceNode::sendRegistrationRequest: failed to create timer";
	if (timer_settime(pimpl->regTimer, 0, &pimpl->regTimerSpec, NULL) == -1)
		throw "DeviceNode::sendRegistrationRequest: failed to arm timer";
	pimpl->regTimerStarted = true;
}

void
DeviceNode::stopRegistrationTimer()
{
	if (pimpl->regTimerStarted) {
		timer_delete(pimpl->regTimer);
		pimpl->regTimerStarted = false;
	}
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
