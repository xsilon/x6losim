#include "sim/Simulator.hpp"
#include "log/log.hpp"

#include <getopt.h>
#include <stdlib.h>
#include <signal.h>

#include "iostream"
using namespace std;

#define X6LOSIM_VERSION		"0.1.0"

static NetworkSimulator * sim;


static void
signal_handler(int signum) {
	int i;
	switch (signum) {
	case SIGINT:
	case SIGTERM:
		xlog(LOG_NOTICE, "x6losim terminate detected, attempting to stop");
		if(sim)
			sim->stop();
		break;
	case SIGHUP:
		break;
	}
	/* Re-arm signal */
	signal(signum, signal_handler);
}

int main( int argc, char* argv[] )
{
	sigset_t sigset, oldset;
	int opt;
	bool debug = false;

	while ((opt = getopt(argc, argv, "x")) != -1) {
		switch (opt) {
		case 'x':
			debug = true;
			break;
		default: /* '?' */
			cerr << "Usage: " << argv[0] << " [-x]" << endl;
			exit(EXIT_FAILURE);
		}
	}

	xlog(LOG_NOTICE, "x6losim started v%s", X6LOSIM_VERSION);
	/* Install the signal handler */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGHUP);
	sigprocmask(SIG_UNBLOCK, &sigset, &oldset);
	if (signal(SIGINT, signal_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);


	try {
		sim = new NetworkSimulator(debug);
		sim->start();
	} catch(...) {
		xlog(LOG_ERR, "Exception caught");
	}
	delete(sim);

	xlog(LOG_NOTICE, "Hasta la vista ....  baby!!!");
	xlog(LOG_NOTICE, "x6losim has finished");
	exit(EXIT_SUCCESS);
}

