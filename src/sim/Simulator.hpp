/*
 * Simulator.hpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */

#ifndef SIM_SIMULATOR_HPP_
#define SIM_SIMULATOR_HPP_

#include "x6losim_interface.h"
#include <stdint.h>
#include <list>
#include <time.h>

// _______________________________________________________ Default Configuration

#define EPOLL_MAX_EVENTS		(64)
#define REGISTRATION_TIME		(120)

// ________________________________________________________________ NetSimPacket

class DeviceNode;
class NetSimPacket
{
public:
	NetSimPacket(netsim_data_ind_pkt *dataInd, DeviceNode *fromNode);
	virtual ~NetSimPacket();

	uint8_t * buf() { return pktBuffer; }
	size_t bufSize() { return pktBufferLen; }

	int getTimeOnWire()
	{
		//TODO: Use Tx parameters to work out the time on the wire
		return 4;
	}

	bool hasCollided()
	{
		return collision;
	}
	void setCollided(bool val)
	{
		collision = val;
	}
	int getTxDoneResult()
	{
		return txDoneResult;
	}
	void setTxDoneResult(int result)
	{
		txDoneResult = result;
	}

	void setRSSI(int8_t rssi)
	{
		((netsim_data_ind_pkt *)pktBuffer)->rssi = rssi;
	}

	void generateChecksum();

	DeviceNode *getFromNode()
	{
		return fromNode;
	}
private:
	size_t pktBufferLen;
	uint8_t *pktBuffer;
	DeviceNode *fromNode;
	bool collision;
	int txDoneResult;
};

typedef std::list<NetSimPacket *> NetSimPktList;

// _________________________________________________________ Device Node classes

//enum DeviceNodeState {
//	DEV_NODE_STATE_UNREG = 0,
//	DEV_NODE_STATE_REGISTERING,
//	DEV_NODE_STATE_ACTIVE,
//	DEV_NODE_STATE_TX,
//	DEV_NODE_STATE_DEREGISTERING
//};

class PhysicalMedium;
class DeviceNode_pimpl;
class DeviceNode
{
friend class RegisteringState;
friend class ActiveState;
friend class TxState;

public:
	DeviceNode(int sockfd);
	virtual ~DeviceNode();

	uint64_t getNodeId();
	const char *getName();
	int getSocketFd();

	PhysicalMedium *getMedium();
	void setMedium(PhysicalMedium *medium);

	NetSimPacket * getTxPacket();
	void setTxPacket(NetSimPacket *pkt);

	bool hasRegTimerExpired();
	bool hasTxTimerExpired();

	void readMsg();
	void sendRegistrationRequest();
	void sendDeregistrationConfirm();
	void sendCcaConfirm(bool result);
	void sendTxDoneIndication(int result);


	// IDeviceNodeState implementation
	void handleRegTimerExpired();
	void handleTxTimerExpired();
	void handleRegistrationConfirm(node_to_netsim_registration_con_pkt *regCon);
	void handleDeregistrationRequest(node_to_netsim_deregistration_req_pkt *deregReq);
	void handleCcaRequest(node_to_netsim_cca_req_pkt *ccaReq);
	void handleDataIndication(netsim_data_ind_pkt *dataInd);

private:
	DeviceNode_pimpl * pimpl;

	/* Disable copy constructor and assigned operator */
	DeviceNode(DeviceNode const&) = delete;
	void operator=(DeviceNode const&) = delete;

	void startRegistrationTimer();
	void stopRegistrationTimer();

	void startTxTimer(int msTimeout);
	void stopTxTimer();
};

class HanaduDeviceNode_pimpl;
class HanaduDeviceNode : public DeviceNode
{
public:
	HanaduDeviceNode(int sockfd);
	virtual ~HanaduDeviceNode();
private:
	HanaduDeviceNode_pimpl *pimpl;
};

class WirelessDeviceNode_pimpl;
class WirelessDeviceNode : public DeviceNode
{
public:
	WirelessDeviceNode(int sockfd);
	virtual ~WirelessDeviceNode();
private:
	WirelessDeviceNode_pimpl *pimpl;
};


// _____________________________________________________ Physical Medium classes

class PhysicalMedium_pimpl;
class PhysicalMedium
{
friend class PowerlineMedium;
public:
	PhysicalMedium(const char * name, int port, int mcastPort, clockid_t clockidToUse);
	virtual ~PhysicalMedium();

	int start();
	void stop();
	void waitForExit();

	bool isIdle();

	void registerNode(DeviceNode *node);
	void deregisterNode(DeviceNode *node);

	void addNodeToCcaList(DeviceNode *node);
	void addNodeToTxList(DeviceNode *node);
	void removeNodeFromTxList(DeviceNode *nodeIn);

	void setPktForTransmission(NetSimPacket *packet);
	void txPacket();

protected:
	void addNode(DeviceNode *node);
	void processCcaList();
	virtual void txCollisionCheck() = 0;

private:
	PhysicalMedium_pimpl * pimpl;

	void processPollerEvents(int numEvents);
	void checkNodeRegistrationTimeout();
	void checkNodeTxTimeout();

	void interval(int waitms);
	void * run();

	/* Disable copy constructor and assigned operator */
	PhysicalMedium(PhysicalMedium const&) = delete;
	void operator=(PhysicalMedium const&) = delete;

	static void *run_helper(void * thisarg) {
		return ((PhysicalMedium *)thisarg)->run();
	}
};

class PowerlineMedium : public PhysicalMedium
{
public:
	PowerlineMedium(int port, clockid_t clockidToUse) :
		PhysicalMedium("PLC", port, HANADU_MCAST_TX_PORT, clockidToUse)
	{
	}

	void addNode(HanaduDeviceNode *node);
	void removeNode(HanaduDeviceNode *node);
protected:
	void txCollisionCheck();

};

class WirelessMedium : public PhysicalMedium
{
public:
	WirelessMedium(int port, clockid_t clockidToUse) :
		PhysicalMedium("AIR", port, WIRELESS_MCAST_TX_PORT, clockidToUse)
	{

	}

	void addNode(WirelessDeviceNode *node);
	void removeNode(WirelessDeviceNode *node);
protected:
	void txCollisionCheck();
};


// ___________________________________________________ Network Simulator classes

enum AcceptStatus
{
	ACCEPT_UNBLOCK = 1,
	ACCEPT_OK = 0,
	ACCEPT_TIMEOUT = -1,
	ACCEPT_ERROR = -2
};

class SocketUnblocker;
class NetworkSimulator_pimpl;
class NetworkSimulator
{
public:
	NetworkSimulator(bool debug = false);
	virtual ~NetworkSimulator();

	void start(void);
	void stop(void);
	static SocketUnblocker& getUnblocker();
	static clockid_t getClockId();


private:
	NetworkSimulator_pimpl * pimpl;

	/* Disable copy constructor and assigned operator */
	NetworkSimulator(NetworkSimulator const&) = delete;
	void operator=(NetworkSimulator const&) = delete;

	AcceptStatus
	acceptConnections(int *hanClient, int *airClient);
	int
	setupAcceptFdSet();
};


#endif /* SIM_SIMULATOR_HPP_ */
