/*
 * Simulator.hpp
 *
 *  Created on: 6 Nov 2014
 *      Author: martin
 */

#ifndef SIM_SIMULATOR_HPP_
#define SIM_SIMULATOR_HPP_

class PacketArbitrator_pimpl;
class PacketArbitrator
{
public:
	PacketArbitrator(const char * name, int port);
	virtual ~PacketArbitrator();

	int start();
	void *run();
private:
	static void *run_helper(void * thisarg) {
		return ((PacketArbitrator *)thisarg)->run();
	}
	PacketArbitrator_pimpl * pimpl;
};

class PhysicalMedium
{
public:
	PhysicalMedium(const char * name, int port) {
		pktArbitrator = new PacketArbitrator(name, port);
	}
	virtual ~PhysicalMedium() {
		delete pktArbitrator;
	}
	void startPacketArbitrator() {
		pktArbitrator->start();
	}
private:
	PacketArbitrator *pktArbitrator;
};

class PowerlineMedium : public PhysicalMedium
{
public:
	PowerlineMedium(int port) : PhysicalMedium("PLC", port) {

	}
};

class WirelessMedium : public PhysicalMedium
{
public:
	WirelessMedium(int port) : PhysicalMedium("AIR", port) {

	}
};

class NetworkSimulator_pimpl;
class NetworkSimulator
{
public:
	NetworkSimulator(bool debug = false);
	virtual ~NetworkSimulator();

	int start(void);

private:
	NetworkSimulator_pimpl * pimpl;
};


#endif /* SIM_SIMULATOR_HPP_ */
