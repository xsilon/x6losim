#include "Simulator.hpp"
#include "socket/Socket.hpp"
#include "log/log.hpp"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

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
