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

class NetSimPacket
{
public:
	uint8_t * buf() { return pktBuffer; }
	int bufSize() { return sizeof(pktBuffer); }
private:
	uint8_t pktBuffer[NETSIM_PKT_MAX_SZ];
};

typedef std::list<NetSimPacket *> NetSimPktList;

class PacketArbitrator_pimpl;
class PacketArbitrator
{
public:
	PacketArbitrator(const char * name, int port);
	virtual ~PacketArbitrator();

	int start();
	void *run();

	// This will get the current pkt list that has been filled and switch t
	void getCapturedPackets(NetSimPktList &pktList);
private:
	static void *run_helper(void * thisarg) {
		return ((PacketArbitrator *)thisarg)->run();
	}
	PacketArbitrator_pimpl * pimpl;
};

class PhysicalMedium_pimpl;
class PhysicalMedium
{
public:
	PhysicalMedium(const char * name, int port, clockid_t clockidToUse);
	virtual ~PhysicalMedium();
	void startPacketArbitrator();

	void *run();
private:
	PhysicalMedium_pimpl * pimpl;

	void
	interval(long nanoseconds);

	/* Disable copy constructor and assigned operator */
	PhysicalMedium(PhysicalMedium const&) = delete;
	void operator=(PhysicalMedium const&) = delete;
};

class PowerlineMedium : public PhysicalMedium
{
public:
	PowerlineMedium(int port, clockid_t clockidToUse) :
		PhysicalMedium("PLC", port, clockidToUse)
	{

	}
};

class WirelessMedium : public PhysicalMedium
{
public:
	WirelessMedium(int port, clockid_t clockidToUse) :
		PhysicalMedium("AIR", port, clockidToUse)
	{

	}
};

class DeviceNode_pimpl;
class DeviceNode
{
public:
	DeviceNode(int sockfd);
	virtual ~DeviceNode();

	void registration();

private:
	DeviceNode_pimpl * pimpl;

	/* Disable copy constructor and assigned operator */
	DeviceNode(DeviceNode const&) = delete;
	void operator=(DeviceNode const&) = delete;
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

	void interval(long nanoseconds);
	void start(void);
	void stop(void);
	static SocketUnblocker& getUnblocker();

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
