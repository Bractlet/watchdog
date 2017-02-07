/* > run-as-child.c
 *
 * Run a C-function as a child process, and provides a C function to execute a
 * pathname/argv[] list in the same manner.
 *
 * (c) 2013 Paul S. Crawford (psc@sat.dundee.ac.uk) licensed under GPL v2
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "watch_err.h"
#include "extern.h"

#define WAIT_MS(x) ((x)*1000)

const unsigned long wait_val[] = {
	WAIT_MS(1),
	WAIT_MS(2),		/* 3ms total from 1+2 */
	WAIT_MS(3),		/* 6ms total from 1+2+3 */
	WAIT_MS(4),		/* 10ms */
	WAIT_MS(20),	/* 30ms */
	WAIT_MS(30),	/* 60ms */
	WAIT_MS(40),	/* 100ms */
	WAIT_MS(200),	/* 300ms */
	WAIT_MS(300),	/* 600ms */
	WAIT_MS(400)	/* 1000ms = 1 second */
};

const int num_wait = sizeof(wait_val) / sizeof(unsigned long);

/*
 * Code to (optionally) sync the file system, then execv() the supplied process.
 *
 * This is intended for use with run_func_as_child() below, with typical
 * usage is something like:
 *
 * char *harg[] = {
 *		"/sbin/hwclock",	// The full path.
 *		"hwclock",			// The argv[0] the program expects.
 *		"-w",				// The 1st command line argument argv[1] (and others, as required)
 *		NULL				// Don't forget the NULL terminator!
 * };
 *
 *	run_func_as_child(60, exec_as_func, 0, harg);
 *
 * This should run "/sbin/hwclock -w" with a time-out of 60 seconds, but not
 * syncing the file system first.
 */

int exec_as_func(int flags, void *ptr)
{
	char **arg;
	char *path;
	int err = ECHILD;	/* Assume no child process until know otherwise. */
	struct stat buf;

	if (ptr == NULL)
		return err;

	arg = (char **)ptr;
	path = arg[0];

	if (path == NULL)
		return err;

	/* First check the supplied program is executable. */
	if (stat(path, &buf) != 0) {
		err = errno;
		log_message(LOG_ERR, "can't get status of %s (errno = %d = '%s')", path, err, strerror(err));
	} else if ((buf.st_mode & S_IXUSR) == 0) {
		err = EACCES; /* Treat as 'access denied' */
		log_message(LOG_ERR, "program %s is not executable", path);
	} else {
		/* If desired, sync file system first. */
		if (flags & FLAG_CHILD_SYNC) {
			if (verbose) {
				log_message(LOG_DEBUG, "syncing file system...");
			}
			sync();
		}

		/* We can also use the flags to re-direct stdout/stderr for "test" and "repair" */
		reopen_std_files(flags);

		if (verbose) {
			/* Create single string with all command line options. */
			int ii = 1;
			char *opt = strdup(":");
			while (opt != NULL && arg[ii] != NULL) {
				opt = realloc(opt, strlen(opt) + strlen(arg[ii]) + 2);
				opt = strcat(opt, " ");
				opt = strcat(opt, arg[ii]);
				ii++;
			}

			if (opt) {
				log_message(LOG_DEBUG, "running %s%s", path, opt);
				free(opt);
			}
		}

		/*
		 * Finally run the child process. The execv() call will not return if
		 * successful, otherwise we return the failure code.
		 */
		execv(path, arg + 1);
		err = errno;
	}

	return err;
}

/*
 * Function to run a supplied function as a child process and passing the two arguments to it.
 * Intended for doing stuff that might fail or block so we can recover in some way and not have
 * the hardware watchdog reset unexpectedly.
 *
 * Calling arguments are:
 *		timeout	= Time to wait until sending SIGTERM then SIGSTOP to child.
 *		funcptr	= Function to be run by the child process.
 *		code	= Integer 1st argument to funcptr()
 *		ptr		= General pointer as 2nd argument to funcptr()
 *
 * Return value indicates the success, or otherwise, of running the child. Return value of zero
 * requires the child to run funcptr() successfully AND for funcptr() to return 0 for success.
 *
 * NOTE: The waitpid() function will not work as expected (gives error of child not present on
 * termination) if the SIGCHLD signal is set to SIG_IGN.
 */

int run_func_as_child(int timeout,
					int (*funcptr) (int, void *),
					int code,
					void *ptr)
{
	pid_t child_pid;
	int err;

	if (funcptr == NULL) {
		return ECHILD;	/* No child process! */
	}

	if (--timeout < 0) {
		timeout = 0; /* Correct for the short delays, and stop -ve errors. */
	}

	child_pid = fork();
	if (child_pid < 0) {
		/* If fork() failed, things are bad so reboot now. */
		err = errno;
		log_message(LOG_ERR, "process fork failed with error = %d = '%s'", err, strerror(err));
		return EREBOOT;
	} else if (child_pid == 0) {
		/* We are the 'child' so run passed generic function. */
		err = (*funcptr) (code, ptr);
		exit(err);
	} else {
		/* We are the parent, so wait for child to stop. */
		int ret, ii;
		int result = 0;

		if (verbose > 1) {
			log_message(LOG_DEBUG, "waiting on PID=%d...", child_pid);
		}

		for (ii = 0; ii < timeout + num_wait; ii++) {
			/* Keep waiting while watchdog kept alive. */
			keep_alive();

			if (ii < num_wait) {
				usleep(wait_val[ii]);	/* sequence of short delays for < 1s. */
			} else {
				usleep(1000000);		/* then 1.0s waits for "timeout" total seconds. */
			}

			ret = waitpid(child_pid, &result, WNOHANG);
			err = errno;

			if (ret < 0) {
				/* Error case. */
				log_message(LOG_ERR, "failed to get child status (PID=%d, error = %d = '%s')", child_pid, err, strerror(err));
				return err;
			}

			if (ret > 0) {
				/* Something has changed, see if it is termination of process. */
				if (WIFEXITED(result)) {
					int ecode = WEXITSTATUS(result);
					if (verbose > 1) {
						log_message(LOG_DEBUG, "child PID=%d has exited with value %d (count=%d)", child_pid, ecode, ii);
					}
					return ecode;
				} else if (WIFSIGNALED(result)) {
					log_message(LOG_WARNING, "child PID=%d was terminated by signal %d", child_pid, WTERMSIG(result));
					return ECHKILL;
				}
			}
		}

		/*
		 * Completed for() waiting loop without the process exiting so try to kill it, and report this
		 * as a time-out rather than as "child process killed" (which implies an external signal did it).
		 */
		kill_process_tree(child_pid, SIGTERM);
		safe_sleep(2);
		ret = waitpid(child_pid, &result, WNOHANG);

		if (ret == 0 || (ret > 0 && !(WIFEXITED(result) || WIFSIGNALED(result)))) {
			/* Seems that SIGTERM did not work, try non-ignorable signal. */
			kill_process_tree(child_pid, SIGKILL);
			/* Get the result to stop appearance of this as a zombie process. */
			usleep(1000);
			waitpid(child_pid, &result, WNOHANG);
		}

		log_message(LOG_ERR, "child timed out (PID=%d)", child_pid);
	}

	return ETOOLONG;
}
