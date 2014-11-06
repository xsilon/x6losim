/* _____________________________________________________________________ x6losim

    Xsilon Network Simulator using QEMU Virtual 802.15.4 Nodes.

    Martin Townsend
        email: martin.townsend@xsilon.com
        email: mtownsend1973@gmail.com
        skype: mtownsend1973
    All Rights Reserved Xsilon Ltd 2014.
 */
#include "x6losim_interface.h"

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
#define X6LOSIM_VERSION		"0.1.0"

/* _______________________________________________________ Function Declarations
 */

/* ___________________________________________________________ Data Declarations
 */

static struct x6lo {
	int loglevel;

	/* inbound unicast socket */
	struct {
		int sockfd;
		int port;
		struct sockaddr_in servaddr;
	} rx;

	struct {
		/* outbound multicast socket */
		int mcastsockfd;
		int mcastport;
		struct sockaddr_in servaddr;
	} tx;

	char pktbuf[256];

	bool debug;
	bool running;
	bool quit;
} sim;

/* __________________________________________________ Local Function Definitions
 */

#define xlog(l, fmt, args...) _xlog(__FILE__, __FUNCTION__, __LINE__, l, fmt, ## args)
#define xlog_hexdump(l, p, len) _xlog_log_hexdump(__FILE__, __FUNCTION__, __LINE__, l, p, len, 0)

/**
 * @brief Log a debug msg.
 * @param file Not used.
 * @param function Function name,
 * @param line Line number
 * @param level Log Level
 * @param fmt Format string.
 */
void
_xlog(const char * file, const char * function, int line, int level,
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

void
_xlog_log_hexdump(
	const char * file,
	const char * function,
	uint16_t line_no,
	int pri,
	const void * start_addr,
	uint16_t dump_byte_len,
	const void * print_address)
{
#define HEXDUMP_BYTES_PER_LINE		(16)
#define HEXDUMP_MAX_HEX_LENGTH		(80)
	char *hex_buff_p;
	char * char_buff_p;
	char *hex_wr_p;
	const char *dump_p = (const char*) start_addr;
	unsigned long row_start_addr = (unsigned long)print_address
	& ~((unsigned long) (HEXDUMP_BYTES_PER_LINE - 1));
	unsigned int first_row_start_column = (unsigned long)print_address % HEXDUMP_BYTES_PER_LINE;
	unsigned int column = 0;
	unsigned int bytes_left = dump_byte_len;
	unsigned int i;

	hex_buff_p = (char *)malloc(HEXDUMP_MAX_HEX_LENGTH + 1);
	char_buff_p = (char *)malloc(HEXDUMP_BYTES_PER_LINE + 1);
	hex_wr_p = hex_buff_p;

	if (pri <= sim.loglevel) {
		// Print the lead in
		for (i = 0; i < first_row_start_column; i++) {
			hex_wr_p += sprintf(hex_wr_p, ".. ");
			char_buff_p[column++] = ' ';
		}

		while (bytes_left) {
			hex_wr_p += sprintf(hex_wr_p, "%02X ", ((unsigned int)*dump_p) & 0xFF);
			if ((*dump_p >= ' ') && (*dump_p <= '~')) {
				char_buff_p[column] = *dump_p;
			} else {
				char_buff_p[column] = '.';
			}

			dump_p++;
			column++;
			bytes_left--;

			if (column >= HEXDUMP_BYTES_PER_LINE) {
				// Print the completed line
				hex_buff_p[HEXDUMP_MAX_HEX_LENGTH] = '\0';
				char_buff_p[HEXDUMP_BYTES_PER_LINE] = '\0';

				_xlog(file, function, line_no, pri,
					  "0x%08X: %s  [%s]\n", row_start_addr, hex_buff_p, char_buff_p);

				row_start_addr += HEXDUMP_BYTES_PER_LINE;
				hex_wr_p = hex_buff_p;
				column = 0;
			}
		}

		if (column) {
			// Print the lead out
			for (i = column; i < HEXDUMP_BYTES_PER_LINE; i++) {
				hex_wr_p += sprintf(hex_wr_p, ".. ");
				char_buff_p[i] = ' ';
			}

			hex_buff_p[HEXDUMP_MAX_HEX_LENGTH] = '\0';
			char_buff_p[HEXDUMP_BYTES_PER_LINE] = '\0';

			_xlog(file, function, line_no, pri,
				  "0x%08X: %s  [%s]\n", row_start_addr, hex_buff_p, char_buff_p);
		}
	}

	free(hex_buff_p);
	free(char_buff_p);
}

static void
signal_handler(int signum) {
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

static void
daemonize(void) {
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

static void
x6losim_send(uint8_t * data, uint16_t len)
{
	sendto(sim.tx.mcastsockfd, data, len, 0,
	       (struct sockaddr *)&sim.tx.servaddr, sizeof(sim.tx.servaddr));
}

static void
x6losim_recv(uint8_t * data, uint16_t len)
{
	struct netsim_pkt_hdr * hdr = (struct netsim_pkt_hdr *)data;
	uint8_t * p = data;

	/* p points to the actual 802.15.4 frame */
	p += NETSIM_PKT_HDR_SZ;

	xlog(LOG_INFO, "Rx Pkt: len:%d repcode:%d ", hdr->psdu_len, hdr->rep_code);
	xlog_hexdump(LOG_INFO, p, hdr->psdu_len);

	/* @todo set RSSI on a per link basis */
	hdr->rssi = -10;
	x6losim_send((uint8_t *)hdr, len);
}

/* _________________________________________________ Global Function Definitions
 */

int
main(int argc, char *argv[]) {
	int rc;
	sigset_t sigset, oldset;
	int opt;
	int pkts_rx = 0;

	sim.rx.port = 11555;
	sim.loglevel = LOG_DEBUG;

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


	sim.rx.sockfd=socket(AF_INET,SOCK_DGRAM,0);
	if (sim.rx.sockfd == -1) {
		perror("Server socket creation failed");
		exit(EXIT_FAILURE);
	}

	bzero(&sim.rx.servaddr,sizeof(sim.rx.servaddr));
	sim.rx.servaddr.sin_family = AF_INET;
	sim.rx.servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	sim.rx.servaddr.sin_port=htons(sim.rx.port);
	if (bind(sim.rx.sockfd, (struct sockaddr *)&sim.rx.servaddr,
		 sizeof(sim.rx.servaddr)) != 0) {
		perror("Bind failed for Netsim server socket");
		exit(EXIT_FAILURE);
	}

	sim.tx.mcastsockfd=socket(AF_INET,SOCK_DGRAM,0);

	bzero(&sim.tx.servaddr,sizeof(sim.tx.servaddr));
	sim.tx.servaddr.sin_family = AF_INET;
	sim.tx.servaddr.sin_addr.s_addr=inet_addr("224.1.1.1");
	sim.tx.servaddr.sin_port=htons(22411);

	sim.running = true;
	xlog(LOG_NOTICE, "UDP Server started");
	xlog(LOG_NOTICE, "Port: %d", sim.rx.port);
	while (sim.running && !sim.quit) {
		struct sockaddr_in cliaddr;
		socklen_t len;
		int n;

		len = sizeof(cliaddr);
		n = recvfrom(sim.rx.sockfd, sim.pktbuf, sizeof(sim.pktbuf), MSG_DONTWAIT,
					 (struct sockaddr *)&cliaddr, &len);

		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
		} else if (n == 0) {
			continue;
		}

		x6losim_recv(sim.pktbuf, n);

		xlog(LOG_INFO, "%d: Received packet %d from %s", n, pkts_rx,
			 inet_ntoa(cliaddr.sin_addr));
		pkts_rx++;
	}

	xlog(LOG_NOTICE, "Hasta la vista ....  baby!!!");
	xlog(LOG_NOTICE, "x6losim has finished");

	return EXIT_SUCCESS;
}

