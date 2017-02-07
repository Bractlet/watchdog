/* > send-email.c
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <unistd.h>		/* for gethostname() etc */
#include <netdb.h>		/* for gethostbyname() */
#include <sys/param.h>	/* for MAXHOSTNAMELEN */

#include <sys/stat.h>
#include <sys/types.h>

#include "extern.h"
#include "watch_err.h"

/*
 * Attempt to send an email to admin about our exit condition. If successful it returns 0
 * otherwise it returns an error code. Once sent, remember and allow a few seconds for the email to
 * go before killing processes!
 *
 * Argument 'ptr' is not really used and must be NULL. Just for run_func_as_child() compatibility.
 */

int send_email(int errorcode, void *ptr)
{
	FILE *ph;
	char exe[512];
	struct stat buf;
	int rv = EACCES; /* Report 'no access' if we can't run sendmail for any reason. */
	const char *sendmail_bin = PATH_SENDMAIL;

	if (admin == NULL || sendmail_bin == NULL) {
		return 0; /* Report OK if not configured for email. */
	}

	/* Only can send an email if sendmail binary exists so check
	 * that first, or else we will get a broken pipe in pclose.
	 * We cannot let the shell check, because a non-existent or
	 * non-executable sendmail binary means that the pipe is closed faster
	 * than we can write to it.
	 */
	if ((stat(sendmail_bin, &buf) != 0) || ((buf.st_mode & S_IXUSR) == 0)) {
		log_message(LOG_ERR, "%s does not exist or is not executable (errno = %d)", sendmail_bin, errno);
	} else {
		snprintf(exe, sizeof(exe), "%s -i %s", sendmail_bin, admin);
		ph = popen(exe, "w");
		if (ph == NULL) {
			rv = errno;
			log_message(LOG_ERR, "cannot start %s (errno = %d)", sendmail_bin, errno);
		} else {
			char myname[MAXHOSTNAMELEN + 1];
			struct hostent *hp;
			rv = 0;

			/* get my name */
			gethostname(myname, sizeof(myname));

			fprintf(ph, "To: %s\n", admin);

			rv |= ferror(ph);

			/* if possible use the full name including domain */
			if ((hp = gethostbyname(myname)) != NULL)
				fprintf(ph, "Subject: %s is going down!\n\n", hp->h_name);
			else
				fprintf(ph, "Subject: %s is going down!\n\n", myname);

			rv |= ferror(ph);

			if (errorcode == ETOOHOT)
				fprintf(ph, "Message from watchdog:\nIt is too hot to keep on working. The system will be halted!\n");
			else
				fprintf(ph,	"Message from watchdog:\nThe system will be rebooted because of error %d!\n", errorcode);

			rv |= ferror(ph);

			if (rv) {
				rv = errno;
				log_message(LOG_ERR, "cannot send mail (errno = %d)", errno);
			}

			if (pclose(ph) == -1) {
				rv = errno;
				log_message(LOG_ERR, "cannot finish mail (errno = %d)", errno);
			}

			if (rv == 0) {
				sleep(10);	/* make sure email is sent if all OK. */
			}
		}
	}

	return rv;
}
