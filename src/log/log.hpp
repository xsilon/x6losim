#ifndef _LOGGING_HPP
#define _LOGGING_HPP

#include <syslog.h>
#include <stdint.h>

extern int loglevel;

void
_xlog(const char * file, const char * function, int line, int level,
      const char *fmt, ...);

void
_xlog_log_hexdump(
	const char * file,
	const char * function,
	uint16_t line_no,
	int pri,
	const void * start_addr,
	uint16_t dump_byte_len,
	const void * print_address);

#define xlog(l, fmt, args...) _xlog(__FILE__, __FUNCTION__, __LINE__, l, fmt, ## args)
#define xlog_hexdump(l, p, len) _xlog_log_hexdump(__FILE__, __FUNCTION__, __LINE__, l, p, len, p)

#endif
