/* > kill_all.c
 *
 * Parts of the following two functions are taken from Miquel van
 * Smoorenburg's killall5 program.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _XOPEN_SOURCE 500	/* for getsid(2) */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>

#include "xmalloc.h"
#include "logmessage.h"
#include "extern.h"

static int read_proc_line(pid_t pid, const char *opt, size_t bsize, char *buf);

#define MORTAL_GID (110) /* A value of GID above rsyslog's GID. */

#define VERY_VERBOSE 0	/* Debug option, normally zero. */

#define DEBUG_DUMP 	1	/* Set to 1 for verbose mode dumping of process lists. */

#if DEBUG_DUMP
#include <time.h>
#include "read-conf.h" /* For trim_white() */
static FILE *dmp_fp = NULL;

/*
 * Open the file for dumping all of the running process information
 * that readproc() gathers.
 */

static void open_dmp(int sig, int aflag)
{
	char fname[1024];
	static int done = 0;

	if (verbose) {
		snprintf(fname, sizeof(fname), "%s/killall5.log", logdir);

		if (done == FALSE) {
			done = TRUE;
			dmp_fp = fopen(fname, "w");	/* First time open and truncate file */
		} else {
			dmp_fp = fopen(fname, "a+"); /* Then append for writing. */
		}

		if (dmp_fp != NULL) {
			log_message(LOG_DEBUG, "Opened dump file %s", fname);
			time_t tnow = time(NULL);
			fprintf(dmp_fp, "sig = %d aflag = %d on %s", sig, aflag, ctime(&tnow));
		} else {
			log_message(LOG_ERR, "Failed opening dump file %s (%s)", fname, strerror(errno));
		}
	}
}

static void write_dmp(pid_t pid, char *buf)
{
	if (dmp_fp != NULL) {
		char cmd[1024];
		read_proc_line(pid, "cmdline", sizeof(cmd), cmd);

		trim_white(cmd);
		trim_white(buf);

		fprintf(dmp_fp, "%s\n%s\n\n", cmd, buf);
	}
}

static void close_dmp(void)
{
	if (dmp_fp != NULL) {
		time_t tnow = time(NULL);
		fprintf(dmp_fp, "Done at %s", ctime(&tnow));
		if (fclose(dmp_fp) < 0) {
			log_message(LOG_ERR, "Error closing dump file (%s)", strerror(errno));
		}
		dmp_fp = NULL;
	}
}
#endif /*DEBUG_DUMP*/

/* Info about a process. */
typedef struct _proc_ {
	pid_t	pid;			/* Process ID.				*/
	int		sid;			/* Session ID.				*/
	pid_t	ppid;			/* Parent's PID.			*/
	struct _proc_ *next;	/* Pointer to next struct.	*/
} PROC;

static PROC *plist = NULL;

/* Free (global here) process list when allocated. */
static void free_plist(void)
{
	PROC *p, *n;

	n = plist;
	for (p = plist; n != NULL; p = n) {
		n = p->next;
		free(p);
	}
	plist = NULL;
}

#ifndef BUFFER_SIZE
#define BUFFER_SIZE (256+1) /* read BUFFER_SIZE-1 so make it multiple of machine word size. */
#endif

/*
 * Read the text line for a given PID's entry of /proc/$PID/$opt
 *
 * This also converts any 'nul' in the string so it can be handled for
 * debug messages more easily. Calling arguments are:
 *
 * pid		: Process to open file.
 * opt		: The file-like option /proc/$PID/$opt to read.
 * bsize	: Buffer size to read into (i.e. buf[] below).
 * buf[]	: The buffer to fill.
 *
 * Return value is zero if successful read, or -1 otherwise.
 */

static int read_proc_line(pid_t pid, const char *opt, size_t bsize, char *buf)
{
	int fd, nread;
	char fname[BUFFER_SIZE];
	int rv = -1;

	fname[BUFFER_SIZE-1] = 0;
	buf[0] = 0;

	snprintf(fname, sizeof(fname)-1, "/proc/%d/%s", (int)pid, opt);
	fd = open(fname, O_RDONLY);

	if (fd < 0) {
		log_message(LOG_ERR, "failed to open %s (%s)", fname, strerror(errno));
	} else {
		nread = read(fd, buf, bsize-1);

		if (nread > 0) {
			int ii;
			/* Force 'nul' terminator. */
			buf[nread] = 0;
			/* Convert any 'nul' separators (e.g. the command line arguments)
			 * in to spaces for string handling & readability in text file.
			 */
			for (ii=0; ii<nread; ii++) {
				if(buf[ii] == 0)
					buf[ii] = ' ';
			}
			rv = 0;	/* Indicate useful data read. */
		}
		close(fd);
	}

	return rv;
}

/*
 * Read the contents of /proc/$PID/stat to get extra info. Typically this
 * looks like:
 *		more /proc/self/stat
 *		16258 (more) R 2492 16258 2492 34827 16258 4202496 321 0 0 0 0 0 0...
 *
 * With the contents given ("man proc") as:
 *
 * Status information about the process. This is used by ps(1). It is defined in /usr/src/linux/fs/proc/array.c
 *
 * The fields, in order, with their proper scanf(3) format specifiers, are:
 *  pid %d     The process ID.
 *  comm %s    The filename of the executable, in parentheses. This is visible whether or not...
 *  state %c   One character from the string "RSDZTW" where R is running, S is sleeping in an...
 *  ppid %d    The PID of the parent.
 *  pgrp %d    The process group ID of the process.
 *  session %d The session ID of the process.
 *
 * etc, etc.
 *
 */

static int get_ID(pid_t pid, PROC *p)
{
	char buf[BUFFER_SIZE];
	int rv = -1;

	p->pid = pid;
	/* Safe starting points. */
	p->sid = 0;
	p->ppid = 0;

	if (read_proc_line(pid, "stat", sizeof(buf), buf) == 0) {
		char *s = strrchr(buf, ')');

		if (s != NULL) {
			char state=0;
			int parent=0, pgroup=0, session=0;

			s++; /* Skip past the ')' character to read the state/ppid/pgrp/session data. */

			if (sscanf(s, " %c %d %d %d", &state, &parent, &pgroup, &session) == 4) {
				p->sid = session;
				p->ppid = (pid_t)parent;
				rv = 0;
			}
		}
	}

#if DEBUG_DUMP
	/* Dump only non-kernel entries (i.e. non-zero SID). */
	if (p->sid > 0) {
		write_dmp(pid, buf);
	}
#endif /*DEBUG_DUMP*/

	return rv;
}

/*
 * Get a list of all processes.
 *
 * Return value is the number found, or -1 if error. This may be short
 * if we run out of memory, so when killing processes try twice.
 *
 */
static int readproc(void)
{
	DIR *dir;
	struct dirent *d;
	pid_t act_pid;
	PROC *p;
	int pcount = 0;
	const char dname[] = "/proc";

	/* Open the /proc directory. */
	if ((dir = opendir(dname)) == NULL) {
		log_message(LOG_ERR, "cannot opendir %s (%s)", dname, strerror(errno));
		return (-1);
	}

	free_plist();

	/* Walk through the directory. */
	while ((d = readdir(dir)) != NULL) {

		/* See if this is a process */
		if ((act_pid = atoi(d->d_name)) == 0)
			continue;

		/*
		 * Get a PROC struct. If this fails, which is likely if we have an
		 * out-of-memory error, we return gracefully with what we have managed
		 * so hopefully a 2nd call after killing some processes will give us more.
		 */
		if ((p = (PROC *) calloc(1, sizeof(PROC))) == NULL) {
			log_message(LOG_ERR, "readproc: out of memory at %d", pcount);
			break;
		}

		get_ID(act_pid, p);

		/* Link it into the list. */
		p->next = plist;
		plist = p;
		pcount++;
	}

	/* Done. */
	closedir(dir);
	return pcount;
}

/*
 * Check the UID of each process to decide if it should be killed in
 * the "first round" of shutting down a sick system. In the shutdown
 * program the test was uid < 100 but that also killed syslog on Ubuntu
 * so raised that a bit as it may be useful to see what happened afterwards.
 */

static int is_mortal(pid_t pid)
{
	char path[128];
	struct stat statbuf;
	int rv = -1;

	snprintf (path, sizeof(path), "/proc/%d", pid);
	if (stat (path, &statbuf) == 0) {
		if (statbuf.st_uid < MORTAL_GID) {
			rv = 0;
		}
	}

	return rv;
}

/*
 * Send 'sig' to "all" processes, typically to kill them.
 *
 * This sends SIGSTOP to all, then builds a list of processes to then uses
 * the kill() function to signal to "all" of them, before using SIGCONT to
 * allow them to resume execution.
 *
 * The 'aflag' is used to control how aggressive the signalling is, with aflag=TRUE
 * then everything other than our PID is signalled, with aflag=FALSE we skip our
 * own session (e.g. child processes still not exited), and all which are (or at
 * least appear to be) kernel processes.
 */

void killall5(int sig, int aflag)
{
	PROC *p;
	int sid = -1;
	int pcount, kcount;

	/*
	 *    Ignoring SIGKILL and SIGSTOP do not make sense, but
	 *    someday kill(-1, sig) might kill ourself if we don't
	 *    do this. This certainly is a valid concern for SIGTERM-
	 *    Linux 2.1 might send the calling process the signal too.
	 */

	/* Since we ignore all signals, we don't have to worry here. MM */
	/* Now stop all processes. */
	suspend_logging();
	kill(-1, SIGSTOP);

#if DEBUG_DUMP
	open_dmp(sig, aflag);
#endif /*DEBUG_DUMP*/

	kcount = 0;
	pcount = readproc();

#if DEBUG_DUMP
	close_dmp();
#endif /*DEBUG_DUMP*/

	if (pcount > 0) {
		/* Find out our own 'sid'. */
		for (p = plist; p; p = p->next) {
			if (p->pid == daemon_pid) {
				sid = p->sid;
				break;
			}
		}

		/* Now kill all processes except our own PID and kernel processes (SID=0).
		 * It turns out (at least on upstart-based systems like Ubuntu 12.04) you
		 * need to 'kill' the init process, not because you can succeed, but if you
		 * don't try then various things like syslog get re-spawned and you can't
		 * then cleanly unmount some file systems, etc.
		 */
		for (p = plist; p; p = p->next) {
			if (p->pid != daemon_pid && p->sid != 0) {
				/*
				 * We either kill everyone else (if aflag != 0) or we also
				 * spare our session, and those that appear privileged processes.
				 */
				if (aflag || (p->sid != sid && is_mortal(p->pid))) {
					kill(p->pid, sig);
					kcount++;
				}
#if VERY_VERBOSE
				else {
					if (verbose) {
						log_message(LOG_DEBUG, "skipping PID=%d SID=%d", p->pid, p->sid);
					}
				}
#endif /*VERY_VERBOSE*/
			}
		}
	} else {
		/* An error in getting the process list. Could be we are totally out of memory or file
		 * handles so our last resort is to signal everything. According to the man page for
		 * the kill() function:
		 *
		 *		POSIX.1-2001 requires that kill(-1,sig) send sig to all processes that the calling
		 *		process may send signals to, except possibly for some implementation-defined system
		 *		processes. Linux allows a process to signal itself, but on Linux the call kill(-1,sig)
		 *		does not signal the calling process.
		 *
		 * Thus we should still be able to shut down cleanly, though we might have lost the ability
		 * to log or send emails, etc.
		 */
		kill(-1, sig);
	}

	/* And let them continue. */
	kill(-1, SIGCONT);
	free_plist();
	resume_logging();

#if VERY_VERBOSE
	if (verbose) {
		log_message(LOG_DEBUG, "sent signal %2d to %d of %d processes", sig, kcount, pcount);
	}
#endif /*VERY_VERBOSE*/
}

/*
 * Function to use kill() recursively on a tree of processes. We start with the parent and
 * then go on to all children of that. For each child, we call ourselves to handle the
 * grandchild case and so on. To avoid stack overflow we start with the 'depth' counter
 * at some modest value, and when it gets to zero we go no further down the tree.
 *
 * For each process we find, we stop it with SIGSTOP, and if that returns zero (implying it was
 * still running) we then try any children. Only after doing the children do we signal & resume
 * the parent. Otherwise, on killing a parent, all the children's PIDs becomes 1 as 'init' takes
 * them over.
 *
 * We can't handle the case where the parent exited before a child, or where a child has used
 * setsid to become independent. But then just what the hell was that doing as a test/repair
 * program for the watchdog daemon! BAD DESIGN!
 *
 * NOTE: We don't have any lock on processes other than those we just stopped!
 *
 * So this may not be a completely safe thing to do, though I think by stopping the parents as we
 * traverse along the tree they won't clean-up any just-exited children, so those PIDs should not
 * be re-used (and thus risk being killed by our now out-of-date listing of /proc).
 *
 * Also some processes (e.g. bash) treat SIGSTOP as a kill, not just as a temporary hold, so you
 * can't do the same as killall5() where we are generally stopping the whole machine anyway.
 *
 * HOWEVER: If the watchdog is having to kill a test or repair programs, something is going badly
 * wrong anyway, and the very small chance of killing a new process that is not really our intended
 * target is probably not _that_ important.
 *
 */

static int kill_recursively(pid_t pid, int sig, int depth)
{
	PROC *p;
	int kcount = 0;

	if (--depth < 0) {
		log_message(LOG_WARNING, "recursion limit reached for PID=%d", pid);
		return 0;
	}

	if (kill(pid, SIGSTOP) == 0) {
		/* This process exists so signal all with it as a parent, then itself (i.e. that parent). */
		for (p = plist; p; p = p->next) {
			if (p->ppid == pid) {
				/*
				 * Do this to all children's PID where its parent PID matched our PID and,
				 * by the recursive nature of this function, to any grandchildren as well.
				 */
				kcount += kill_recursively(p->pid, sig, depth);
			}
		}

		if (verbose) {
			log_message(LOG_DEBUG, "sending signal %2d to PID %d (depth %d)", sig, pid, depth);
		}

		kill(pid, sig);
		kill(pid, SIGCONT);
		kcount++;
	}

	return kcount;
}

/*
 * Function to signal with kill() to a parent process and any child processes of it.
 *
 * Used to deal with cases such as a bash script running another program that is the
 * actual block to timely exiting.
 *
 * Return value is the number of processes signalled.
 */

int kill_process_tree(pid_t pid, int sig)
{
	int pcount=0, kcount=0;
	const int MAX_DEPTH = 5; /* The limit on recursion. */

	/*
	 * Try to stop the parent and, if successful, get the current /proc list
	 * and try to signal any children (and grandchildren, etc).
	 */
	if (kill(pid, SIGSTOP) == 0) {
		pcount = readproc();

		if (pcount > 0) {
			kcount = kill_recursively(pid, sig, MAX_DEPTH);
		} else {
			/* Something has gone very wrong with readproc() */
			kill(pid, sig);
			kill(pid, SIGCONT);
		}
	}

	free_plist();

#if VERY_VERBOSE
	if (verbose) {
		log_message(LOG_DEBUG, "sent signal %2d to %d of %d processes", sig, kcount, pcount);
	}
#endif /*VERY_VERBOSE*/

	return kcount;
}
