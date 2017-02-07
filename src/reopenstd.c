/* > reopenstd.c
 *
 * Reopen the stdout & stderr files to watchdog log directory to capture child
 * process' outputs.
 *
 * (c) 2013 Paul S. Crawford (psc@sat.dundee.ac.uk) & Michael Meskes licensed under GPL v2
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "watch_err.h"
#include "extern.h"

static const char *fnames[] = {
	"repair-bin", // Longest name should be first in list.
	"test-bin"
};

static const char *fsuffix[] = {
	".stdout", // Longest name should be first in list.
	".stderr"
};

static char *filename_buf = NULL;
static int buf_length = 0;
static int buf_offset = 0;

/*
 * Declare where we want the test/repair program's output to go to. This allocates a suitable
 * buffer so we don't need to worry later about out-of-memory for this (at least!).
 *
 * Call with NULL to free the buffer if needed.
 */

void set_reopen_dir(const char *dname)
{
	/* Release any previous buffer memory */
	if (filename_buf != NULL) {
		free(filename_buf);
	}

	filename_buf = NULL;
	buf_length = 0;
	buf_offset = 0;

	if (dname != NULL) {
		/* Create buffer and copy directory name. We keep a record of the
		 * length of the 'dname' so we can simply copy fnames[]+fsuffix[] to it.
		 *
		 * Need some spare for nul-terminator and possible '/' addition.
		 */
		buf_offset = strlen(dname);
		buf_length = buf_offset + strlen(fnames[0]) + strlen(fsuffix[0]) + 2;
		filename_buf = xcalloc(buf_length, sizeof(char));
		strcpy(filename_buf, dname);

		/* Finally check we have a trailing '/' character, adding one if needed. */
		if (buf_offset > 0) {
			/* We have not specified "" so a trailing '/' is needed. */
			if (filename_buf[buf_offset-1] != '/') {
				filename_buf[buf_offset] = '/';
				buf_offset++;
				filename_buf[buf_offset] = 0;
			}
		}
	}
}

/*
 * Performr the re-open, creating the path/name as required.
 */

static int do_reopen(int idx, FILE *fp, const char *sfx)
{
	int err = 0;
	char *rname = "/dev/null";

	if (idx >= 0) {
		/* Have a specific file to use, not just /dev/null for re-direct. Start
		 * by removing any previous fname[]/fsufix[] stuff.
		 */
		if (buf_length > buf_offset) {
			filename_buf[buf_offset] = 0;
			rname = strcat(filename_buf, fnames[idx]);
			rname = strcat(rname, sfx);
			assert(strlen(rname) < buf_length);
		}
	}

	if (!freopen(rname, "w+", fp)) {
		err = errno;
		log_message(LOG_WARNING, "unable to reopen using %s (%s)", rname, strerror(err));
	} else if (verbose > 1) {
		log_message(LOG_DEBUG, "reopened using %s for idx = %d", rname, idx);
	}

	return err;
}


/*
 * Re-open stdout & stderr to a pair of files in the previously specified directory. The
 * argument 'flags' has bits to signal if it is for "test" or "repair" and chooses names
 * from the above table accordingly. If neither is set, then the do_reopen() function
 * defaults to /dev/null
 *
 * Return value is any error encountered in re-opening the files. Previously this would cause
 * the child to exit, however, with the new use of daemon() those stdout/stderr files are
 * going to /dev/null so it is not such a big deal if you don't have permission to reopen
 * using the watchdog log directory.
 */

int reopen_std_files(int flags)
{
	int err = 0;
	int idx = -1;
	int rv;

	/* If not set (e.g. in foreground mode) simply do nothing. */
	if (filename_buf == NULL) {
		return 0;
	}

	/* Check to see if either specific name is in use. */
	if (flags & FLAG_REOPEN_STD_REPAIR) {
		idx = 0;
	} else if (flags & FLAG_REOPEN_STD_TEST) {
		idx = 1;
	}

	/* Re-open as needed. */
	rv = do_reopen(idx, stdout, fsuffix[0]);
	if (rv)
		err = rv;

	rv = do_reopen(idx, stderr, fsuffix[1]);
	if (rv)
		err = rv;

	return err;
}
