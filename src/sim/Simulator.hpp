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
	PacketArbitrator(const char * name);
	virtual ~PacketArbitrator();

	int start(int port);
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
	PhysicalMedium(const char * name) {
		pktArbitrator = new PacketArbitrator(name);
	}
	virtual ~PhysicalMedium() {
		delete pktArbitrator;
	}
private:
	PacketArbitrator *pktArbitrator;
};

class PowerlineMedium : public PhysicalMedium
{

};

class WirelessMedium : public PhysicalMedium
{

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
