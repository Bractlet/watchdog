#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "extern.h"
#include "watch_err.h"

int check_file_stat(struct list *file)
{
	struct stat buf;

	if (file == NULL) {
		return (ENOERR);
	}

	/* in filemode stat file */
	if (stat(file->name, &buf) == -1) {
		int err = errno;
		log_message(LOG_ERR, "cannot stat %s (errno = %d = '%s')", file->name, err, strerror(err));
		return (err);
	} else if (file->parameter.file.mtime != 0) {
		int twait = (int)(time(NULL) - buf.st_mtime);

		if (twait > file->parameter.file.mtime) {
			/* file wasn't changed often enough */
			log_message(LOG_ERR, "file %s was not changed in %d seconds (more than %d)", file->name, twait, file->parameter.file.mtime);
			return (ENOCHANGE);
		}
		/* do verbose logging */
		if (verbose && logtick && ticker == 1) {
			char text[25];
			/* Remove the trailing '\n' of the ctime() formatted string. */
			strncpy(text, ctime(&buf.st_mtime), sizeof(text)-1);
			text[sizeof(text)-1] = 0;
			log_message(LOG_DEBUG, "file %s was last changed at %s (%ds ago)", file->name, text, twait);
		}
	} else {
		/* do verbose logging */
		if (verbose && logtick && ticker == 1) {
			log_message(LOG_DEBUG, "file %s status OK", file->name);
		}
	}
	return (ENOERR);
}

/*
 * Present check_file_stat() in manner for run_func_as_child() to call.
 * In this case 'code' is not used.
 */

static int run_func(int code, void *ptr)
{
	return check_file_stat((struct list *)ptr);
}

/*
 * An alternative to check_file_stat() that forks the process to run
 * it as a child, so a time-out on NFS access, etc, won't trigger a hardware
 * reset, so the main daemon has a chance to reboot cleanly.
 */

int check_file_stat_safe(struct list *file)
{
	const int CHECK_TIMEOUT = 5;
	int ret;

	if (file == NULL) {
		return (ENOERR);
	}

	ret = run_func_as_child(CHECK_TIMEOUT, run_func, 0, file);

	if (ret == ETOOLONG) {
		log_message(LOG_ERR, "timeout getting file status for %s", file->name);
	}

	return (ret);
}
