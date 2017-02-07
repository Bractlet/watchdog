#ifndef _WATCH_ERR_H
#define _WATCH_ERR_H

/*********************************/
/* additional error return codes */
/*********************************/

#define ENOERR		0	/* no error */
#define EREBOOT		255	/* unconditional reboot (255 = -1 as unsigned 8-bit) */
#define ERESET		254	/* unconditional hard reset */
#define EMAXLOAD	253	/* load average too high */
#define ETOOHOT		252	/* too hot inside */
#define ENOLOAD		251	/* /proc/loadavg contains no data */
#define ENOCHANGE	250	/* file wasn't changed in the given interval */
#define EINVMEM		249	/* /proc/meminfo contains invalid data */
#define ECHKILL		248	/* child was killed by signal */
#define ETOOLONG	247	/* child didn't return in time */
#define EUSERVALUE	246	/* reserved for user error code */
#define EDONTKNOW	245	/* unknown, not "no error" (i.e. success) but implies test still running */

#endif /*_WATCH_ERR_H*/
