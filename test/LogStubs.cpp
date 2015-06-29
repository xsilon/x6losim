/*
 * $Id: ssmd_log_stubs.cc 4206 2012-06-28 08:10:41Z martin $
 * Author: Martin Townsend
 *             email: martin.townsend@power-oasis.com
 *             email: mtownsend1973@gmail.com
 *             skype: mtownsend1973
 *
 * Description:
 *
 * CopyRight PowerOasis Ltd 2010
 *
 */

#include "ssmd_log.h"
 
#include <stdio.h>


int show_log_errors = 0;
int show_log = 0;

/** Description for each debug level */
static const char * level_str[] = {
    "Emergency",    /** LOG_EMERG   0   system is unusable */
    "Alert",        /** LOG_ALERT   1   action must be taken immediately */
    "Critical",     /** LOG_CRIT    2   critical conditions */
    "Error",        /** LOG_ERR     3   error conditions */
    "Warning",      /** LOG_WARNING 4   warning conditions */
    "Notice",       /** LOG_NOTICE  5   normal but significant condition */
    "Info",         /** LOG_INFO    6   informational */
    "Debug",        /** LOG_DEBUG   7   debug-level messages */
};

void
ssmd_log_print(
    const char * file,
    const char * function,
    int line_no,
    int level,
    LOG_GROUP_T group,
    const char * fmt, ...
) {
    va_list args;
    va_start( args, fmt );
    ssmd_log_vprint(file, function, line_no, level, group, fmt, args);
    va_end( args );
}

void
ssmd_log_vprint(
    const char * file,
    const char * function,
    int line_no,
    int level,
    LOG_GROUP_T group,
    const char * fmt, 
    va_list vargs
) {
	if(show_log_errors || show_log) {
		if(level >= LOG_EMERG && level <= LOG_ERR) {
			printf("\033[7m\033[1;31m");
			printf("[%s] %s(%d): ", level_str[level], function, line_no);
			vprintf(fmt, vargs);
			printf("\n");
			printf("\033[0;0m");
			printf("\033[0;0m");
		} else {
		    if(show_log) {
	            printf("\033[0;0m");
	            printf("\033[0;0m");
	            printf("[%s] %s(%d): ", level_str[level], function, line_no);
	            vprintf(fmt, vargs);
	            printf("\n");

		    }
		}
	}
}

