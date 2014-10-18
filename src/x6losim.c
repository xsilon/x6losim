/* _____________________________________________________________________ x6losim

 Description : Xsilon Network Simulator using QEMU Virtual 802.15.4 Nodes.
 Author      : Martin Townsend
                 email: martin.townsend@xsilon.com
                 email: mtownsend1973@gmail.com
                 skype: mtownsend1973
 Copyright   : All Rights Reserved Xsilon Ltd 2014.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* _____________________________________________ Constants and Macro definitions
 */

/* _______________________________________________________ Function Declarations
 */

/* ___________________________________________________________ Data Declarations
 */

static struct x6lo {
	int sockfd;
	int port;
	struct sockaddr_in servaddr;
	char pktbuf[256];

	bool debug;
	bool running;
	bool quit;
} sim;

/* __________________________________________________ Local Function Definitions
 */

#define xlog(l, fmt, args...) _xlog(__FILE__, __FUNCTION__, __LINE__, l, fmt, ## args)

/**
 * @brief Log a debug msg.
 * @param file Not used.
 * @param function Function name,
 * @param line Line number
 * @param level Log Level
 * @param fmt Format string.
 */
void _xlog(const char * file, const char * function, int line, int level,
		const char *fmt, ...) {
	va_list vargs;
	const char * s;

	va_start(vargs, fmt);
	if (sim.debug) {
		if (level == LOG_ERR) {
			/* Change color to red */
			printf("\033[1;31m");
		} else if (level == LOG_NOTICE) {
			/* Change color to green */
			printf("\033[1;32m");
		} else {
			printf("\033[0;0m");
		}
		/* We are going to ignore file as we only have one file */
		printf("[%s:%d]: ", function, line);
		vprintf(fmt, vargs);

		s = fmt;
		while (*s != '\r' && *s != '\n' && *s != '\0') {
			s++;
		}
		if (*s == '\0') {
			printf("\n");
		}

		/* Change colour back to default. */
		printf("\033[0;0m");
	} else {
		vsyslog(level, fmt, vargs);
	}
	va_end(vargs);
}

static void signal_handler(int signum) {
	int i;
	switch (signum) {
	case SIGINT:
	case SIGTERM:
		xlog(LOG_NOTICE, "x6losim quit detected, attempting to stop");
		sim.quit = true;
		break;
	case SIGHUP:
		break;
	}
	/* Re-arm signal */
	signal(signum, signal_handler);
}

static void daemonize(void) {
	pid_t pid, sid;

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		/* Log any failure */
		exit(EXIT_FAILURE);
	}

	/* Close out the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}

/* _________________________________________________ Global Function Definitions
 */

int
main(int argc, char *argv[]) {
	int rc;
	sigset_t sigset, oldset;
	int opt;

	sim.port = 11555;

	while ((opt = getopt(argc, argv, "x")) != -1) {
		switch (opt) {
		case 'x':
			sim.debug = 1;
			break;
		default: /* '?' */
			fprintf(stderr, "Usage: %s [-x]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	/* Ensure we run as a daemon */
	if (!sim.debug) {
		daemonize();
		openlog("x6losim", LOG_CONS, LOG_DAEMON);
	}

	xlog(LOG_NOTICE, "x6losim started");

	/* Install the signal handler */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGHUP);
	sigprocmask(SIG_UNBLOCK, &sigset, &oldset);	if (signal(SIGINT, signal_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);


	sim.sockfd=socket(AF_INET,SOCK_DGRAM,0);

	bzero(&sim.servaddr,sizeof(sim.servaddr));
	sim.servaddr.sin_family = AF_INET;
	sim.servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	sim.servaddr.sin_port=htons(sim.port);
	bind(sim.sockfd, (struct sockaddr *)&sim.servaddr, sizeof(sim.servaddr));

	sim.running = true;
	xlog(LOG_NOTICE, "UDP Server started");
	xlog(LOG_NOTICE, "Port: %d", sim.port);
	while (sim.running && !sim.quit) {
		struct sockaddr_in cliaddr;
		socklen_t len;
		int n;

		len = sizeof(cliaddr);
		n = recvfrom(sim.sockfd, sim.pktbuf, sizeof(sim.pktbuf), MSG_DONTWAIT,
					 (struct sockaddr *)&cliaddr, &len);

		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
		}

		xlog(LOG_INFO, "%d: Received packet from %s", n,
			 inet_ntoa(cliaddr.sin_addr));
	}

	xlog(LOG_NOTICE, "Hasta la vista ....  baby!!!");
	xlog(LOG_NOTICE, "x6losim has finished");

	return EXIT_SUCCESS;
}

