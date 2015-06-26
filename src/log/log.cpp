#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

bool log_to_syslog = false;
int loglevel = LOG_DEBUG;
pthread_mutex_t log_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

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
	pthread_mutex_lock(&log_mutex);
	if (!log_to_syslog) {
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
	pthread_mutex_unlock(&log_mutex);
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

	pthread_mutex_lock(&log_mutex);
	if (pri <= loglevel) {
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
	pthread_mutex_unlock(&log_mutex);

	free(hex_buff_p);
	free(char_buff_p);
}
