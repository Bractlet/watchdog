/* > errorcodes.c
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#include "watch_err.h"
#include "extern.h"

/*
 * In some cases, we are doomed if we try running 'sendmail' or any other process
 * so we might as well start by killing all user processes in an attempt to free
 * up resources in order to try that. This tests for conditions that are likely
 * to demand such actions.
 */

int is_bad_error(int errorcode)
{
	int rv = 0;

	switch (errorcode) {
		case EREBOOT:	/* Unconditional reboot requested - assume the worst! */
		case ERESET:	/* Unconditional hard reset requested - assume the worst! */
		case EMAXLOAD:	/* System too busy? */
		case ETOOHOT:	/* Too hot - not much point in running more actions! */
		case EMFILE:	/* "Too many open files" */
		case ENFILE:	/* "Too many open files in system" */
		case ENOMEM:	/* "Not enough space" */
			rv = 1;
			break;
		default:
			break;
	}

	return rv;
}

/*
 * Extend the operation of the system's strerror() error-to-text mapping function to
 * include errors that are specific to the watchdog code.
 */

const char *wd_strerror(int err)
{
	char *str = "";

	switch (err) {
		case ENOERR:		str = "no error"; break;
		case EREBOOT:		str = "unconditional reboot requested"; break;
		case ERESET:		str = "unconditional hard reset requested"; break;
		case EMAXLOAD:		str = "load average too high"; break;
		case ETOOHOT:		str = "too hot"; break;
		case ENOLOAD:		str = "loadavg contains no data"; break;
		case ENOCHANGE:		str = "file was not changed in the given interval"; break;
		case EINVMEM:		str = "meminfo contains invalid data"; break;
		case ECHKILL:		str = "child process was killed by signal"; break;
		case ETOOLONG:		str = "child process did not return in time"; break;
		case EUSERVALUE:	str = "user-reserved code"; break;
		case EDONTKNOW:		str = "unknown (neither good nor bad)"; break;
		default:			str = strerror(err); break;
	}

	return str;
}
