#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _XOPEN_SOURCE 500	/* for getsid(2) */
#define _BSD_SOURCE		/* for acct(2) */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <netdb.h>
#include <paths.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <utmp.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h> /* For MNT_FORCE  */
#include <sys/swap.h> /* for swapoff() */
#include <unistd.h>
#include <time.h>

#include "watch_err.h"
#include "extern.h"

#if defined __GLIBC__
#include <sys/quota.h>
#include <sys/swap.h>
#include <sys/reboot.h>
#else				/* __GLIBC__ */
#include <linux/quota.h>
#endif				/* __GLIBC__ */

#ifndef NSIG
#define NSIG _NSIG
#endif

#ifndef __GLIBC__
#ifndef RB_AUTOBOOT
#define RB_AUTOBOOT		0xfee1dead,672274793,0x01234567 /* Perform a hard reset now.  */
#define RB_ENABLE_CAD	0xfee1dead,672274793,0x89abcdef /* Enable reboot using Ctrl-Alt-Delete keystroke.  */
#define RB_HALT_SYSTEM	0xfee1dead,672274793,0xcdef0123 /* Halt the system.  */
#define RB_POWER_OFF	0xfee1dead,672274793,0x4321fedc /* Stop system and switch power off if possible.  */
#endif /*RB_AUTOBOOT*/
#endif /* !__GLIBC__ */

/* Local function prototypes. */

static void close_all_but_watchdog(void);
static void panic(void);
static void kill_everything_else(int aflag, int stime);
static void write_wtmp(void);
static void save_urandom(void);
static void unmount_disks_ourselves(void);
static void run_shutdown_children(void);
static void do_brutal_shutdown(void);
static void do_orderly_shutdown(int errorcode);

/*
 * On exit we close the devices and log that we stopped.
 */

void terminate(int ecode)
{
	/* Log the closing message */
	log_message(LOG_NOTICE, "stopping daemon (%d.%d)", MAJOR_VERSION, MINOR_VERSION);
	unlock_our_memory();
	close_all_but_watchdog();
	close_watchdog();
	remove_pid_file();
	close_logging();
	usleep(100000); /* make sure log is written. */
	exit(ecode);
}

/*
 * shut down the system
 */

void do_shutdown(int errorcode)
{
	/* Tell syslog what's happening */
	log_message(LOG_ALERT, "shutting down the system because of error %d = '%s'", errorcode, wd_strerror(errorcode));
	close_all_but_watchdog();

	if (errorcode == ERESET) {
		do_brutal_shutdown();
	} else {
		do_orderly_shutdown(errorcode);
	}

	log_message(LOG_ALERT, "calling reboot() function");

	/* finally reboot */
	if (errorcode != ETOOHOT) {
		if (get_watchdog_fd() != -1) {
			/* We have a hardware timer, try using that for a quick reboot first. */
			set_watchdog_timeout(1);
			sleep(dev_timeout * 4);
		}
		/* That failed, or was not possible, ask kernel to do it for us. */
		reboot(RB_AUTOBOOT);
	} else {
		/* Rebooting makes no sense if it's too hot. */
		if (temp_poweroff) {
			/* Tell system to power off if possible. */
			reboot(RB_POWER_OFF);
		} else {
			/* Turn on hard reboot, CTRL-ALT-DEL will reboot now. */
			reboot(RB_ENABLE_CAD);
			/* And perform the `halt' system call. */
			reboot(RB_HALT_SYSTEM);
		}
	}

	/* unbelievable: we're still alive */
	panic();
}

/*
 * Close all the device except for the watchdog.
 */

static void close_all_but_watchdog(void)
{
	close_loadcheck();
	close_memcheck();
	close_tempcheck();
	close_heartbeat();
	close_netcheck(target_list);

	free_process();		/* What check_bin() was waiting to report. */
	free_all_lists();	/* Memory used by read_config() */
}

/*
 * panic: we're still alive but shouldn't be.
 */

static void panic(void)
{
	int kill_time = dev_timeout * 4;
	/*
	 * Okay we should never reach this point,
	 * but if we do we will cause the hard reset
	 */
	open_logging(NULL, MSG_TO_STDERR | MSG_TO_SYSLOG);
	log_message(LOG_ALERT, "WATCHDOG PANIC: failed to reboot, trying hard-reset");
	sleep(kill_time);

	/* if we are still alive, we just exit */
	log_message(LOG_ALERT, "WATCHDOG PANIC: still alive after sleeping %d seconds", kill_time);
	close_all_but_watchdog();
	close_watchdog();
	close_logging();
	exit(1);
}

/*
 * Kill everything, but depending on 'aflag' spare kernel/privileged
 * processes. Do this twice in case we have out-of-memory problems.
 *
 * The value of 'stime' is the delay from 2nd SIGTERM to SIGKILL but
 * the SIGKILL is only used when 'aflag' is true as things really bad then!
 */

static void kill_everything_else(int aflag, int stime)
{
	int ii;

	/* Ignore all signals (except children, so run_func_as_child() works as expected). */
	for (ii = 1; ii < NSIG; ii++) {
		if (ii != SIGCHLD) {
			signal(ii, SIG_IGN);
		}
	}

	/* Stop init; it is insensitive to the signals sent by the kernel. */
	kill(1, SIGTSTP);

	/* Try to terminate processes the 'nice' way. */
	killall5(SIGTERM, aflag);
	safe_sleep(1);
	/* Do this twice in case we have out-of-memory problems. */
	killall5(SIGTERM, aflag);

	/* Now wait for most processes to exit as intended. */
	safe_sleep(stime);

	if (aflag) {
		/* In case that fails, send them the non-ignorable kill signal. */
		killall5(SIGKILL, aflag);
		keep_alive();
		/* Out-of-memory safeguard again. */
		killall5(SIGKILL, aflag);
		keep_alive();
	}
}

/*
 * Record the system shut-down.
 */

static void write_wtmp(void)
{
	time_t t;
	struct utmp wtmp;
	const char *fname = _PATH_WTMP;
	int fd;

	if ((fd = open(fname, O_WRONLY | O_APPEND)) >= 0) {
		memset(&wtmp, 0, sizeof(wtmp));
		time(&t);
		strcpy(wtmp.ut_user, "shutdown");
		strcpy(wtmp.ut_line, "~");
		strcpy(wtmp.ut_id, "~~");
		wtmp.ut_pid = 0;
		wtmp.ut_type = RUN_LVL;
		wtmp.ut_time = t;
		if (write(fd, (char *)&wtmp, sizeof(wtmp)) < 0)
			log_message(LOG_ERR, "failed writing wtmp (%s)", strerror(errno));
		close(fd);
	}
}

/*
 * Save the random seed if a save location exists.
 * Don't worry about error messages, we react here anyway
 */

static void save_urandom(void)
{
	const char *seedbck = RANDOM_SEED;
	int fd_seed, fd_bck;
	char buf[512];

	if (strlen(seedbck) != 0) {
		if ((fd_seed = open("/dev/urandom", O_RDONLY)) >= 0) {
			if ((fd_bck = creat(seedbck, S_IRUSR | S_IWUSR)) >= 0) {
				if (read(fd_seed, buf, sizeof(buf)) == sizeof(buf)) {
					if (write(fd_bck, buf, sizeof(buf)) < 0) {
						log_message(LOG_ERR, "failed writing urandom (%s)", strerror(errno));
					}
				}
				close(fd_bck);
			}
			close(fd_seed);
		}
	}
}

/*
 * Unmount file ourselves, this code adapted from util-linux-2.17.2/login-utils/shutdown.c
 * However, they also try running the 'umount' binary first, as it might be smarter.
 */

#define NUM_MNTLIST 128

static void unmount_disks_ourselves(void)
{
	/* unmount all disks */

	FILE *mtab;
	struct mntent *mnt;
	char *mntlist[NUM_MNTLIST];
	int i;
	int n;
	const char *fname = _PATH_MOUNTED;

	keep_alive();
	/* Could this hang the system? Hardware watchdog will kick in, but might be
	 * better to try fork() and idle for a while before forcing unmounts?
	 */
	sync();
	keep_alive();

	if (!(mtab = setmntent(fname, "r"))) {
		log_message(LOG_ERR, "could not open %s (%s)", fname, strerror(errno));
		return;
	}

	n = 0;
	while (n < NUM_MNTLIST-1 && (mnt = getmntent(mtab))) {
		/*
		 * Neil Phillips: trying to unmount temporary / kernel
		 * filesystems is pointless and may cause error messages;
		 * /dev can be a ramfs managed by udev.
		 */
		if (strcmp(mnt->mnt_type, "devfs") == 0 ||
			strcmp(mnt->mnt_type, "proc")  == 0 ||
			strcmp(mnt->mnt_type, "sysfs") == 0 ||
			strcmp(mnt->mnt_type, "ramfs") == 0 ||
			strcmp(mnt->mnt_type, "tmpfs") == 0 ||
			strcmp(mnt->mnt_type, "devpts") == 0 ||
			strcmp(mnt->mnt_type, "devtmpfs") == 0)
			continue;
		mntlist[n++] = strdup(mnt->mnt_dir);
	}
	endmntent(mtab);

	/* we are careful to do this in reverse order of the mtab file */

	for (i = n - 1; i >= 0; i--) {
		char *filesys = mntlist[i];

		log_message(LOG_DEBUG, "unmounting %s", filesys);
		keep_alive();

#if defined( MNT_FORCE )
		if (umount2(filesys, MNT_FORCE) < 0) {
#else
		if (umount(filesys) < 0) {
#endif /*!MNT_FORCE*/
			log_message(LOG_ERR, "could not unmount %s (%s)", filesys, strerror(errno));
		}
	}
}

#undef NUM_MNTLIST

/*
 * Stop swap-space on files only, as they can stop file system unmounting. Calling arguments
 * of 'code' and 'ptr' are unused here.
 */

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 256
#endif /*BUFFER_SIZE*/

int swapoff_files(int code, void *ptr)
{
	int err = 0;
	FILE *fp;
	const char sname[] = "/proc/swaps";
	char buf[BUFFER_SIZE];
	char sdev[BUFFER_SIZE];
	char stype[BUFFER_SIZE];

	/* Clear memory as precaution. */
	memset(buf, 0, sizeof(buf));
	sdev[0] = stype[0] = 0;

	fp = fopen(sname, "r");

	if (fp == NULL) {
		err = errno;
		log_message(LOG_ERR, "unable to open %s (errno = %d = '%s')", sname, err, strerror(err));
	} else {
		/* Read file for swap space. */
		while (fgets(buf, BUFFER_SIZE-1, fp) != NULL) {
			if (sscanf(buf, " %s %s", sdev, stype) == 2) {
				/* Skip header line. */
				if (strcmp(sdev, "Filename") == 0)
					continue;

				/* Stop swap files only, as they trouble un-mounting file systems. */
				if (strcmp(stype, "file") == 0) {
					log_message(LOG_INFO, "stopping swap for %s", sdev);
					if (swapoff(sdev)) {
						err = errno;
						log_message(LOG_ERR, "failed to stop swap on %s (errno = %d = '%s')", sdev, err, strerror(err));
					}
				}
			}
		}

		fclose(fp);
	}

	return err;
}

/*
 * If we can, use the system-supplied programs to do things properly, then
 * we fall-back on our simpler methods.
 */

#ifndef PATH_HWCLOCK
#pragma message "Warning - nothing in Makefile for PATH_HWCLOCK"
#define PATH_HWCLOCK	"/sbin/hwclock"
#endif  /*PATH_HWCLOCK*/

#ifndef PATH_SWAPOFF
#pragma message "Warning - nothing in Makefile for PATH_SWAPOFF"
#define PATH_SWAPOFF	"/sbin/swapoff"
#endif	/*PATH_SWAPOFF*/

#ifndef PATH_UMOUNT
#pragma message "Warning - nothing in Makefile for PATH_UMOUNT"
#define PATH_UMOUNT	"/bin/umount"
#endif /*PATH_UMOUNT*/

static void run_shutdown_children(void)
{
	char *hwclock_arg[] = {
		PATH_HWCLOCK,		/* [0] = path to program. */
		"hwclock", "-w",	/* [1] & [2] = Write system time to RTC */
		NULL,				/* [3] = Assume UTC unless told otherwise. */
		NULL,				/* [4] = Disable adjustment file. */
		NULL				/* Don't forget NULL terminator! */
	};
	char *swapof_arg[] = {
		PATH_SWAPOFF,
		"swapoff", "-a",
		NULL	/* Don't forget NULL terminator! */
	};
	char *umount_arg[] = {
		PATH_UMOUNT,
		"umount", "-a", "-t", "nodevfs,devtmpfs",
		NULL	/* Don't forget NULL terminator! */
	};
	/* With few GB of disk cache or swap used, might take couple of minutes to release resources. */
	const int wait_time = 180;

	/*
	 * In most (sensible) cases the real-time (aka CMOS) clock keeps UTC and is used to start
	 * system time when booting, so we want to be sure it is set OK on shutting down/rebooting.
	 */
	if (RTC_is_UTC) {
		hwclock_arg[3] = "--utc";
	} else {
		hwclock_arg[3] = "--localtime";
	}

	/*
	 * If no existing adjustment file (access() returns -1) then disable adjustment file use,
	 * as this implies we are on a modern system that uses the '11 minute mode' where the kernel
	 * is periodically writing the time and we don't have long-term adjustments of the RTC to
	 * estimate a drift rate.
	 */
	if (access("/etc/adjtime", R_OK)) {
		hwclock_arg[4] = "--noadjfile";
	}

	/* Sync RTC (aka hardware clock, CMOS clock) to system time. */
	run_func_as_child(20, exec_as_func, 0, hwclock_arg);

	/* Turn off all swap files, if that fails then try the swapoff binary */
	if (run_func_as_child(wait_time, swapoff_files, 0, NULL)) {
		/* Turn off all swap space. */
		run_func_as_child(wait_time, exec_as_func, 0, swapof_arg);
	}

	/* Sync, and then unmount file systems. */
	run_func_as_child(wait_time, exec_as_func, FLAG_CHILD_SYNC, umount_arg);
}

/*
 * This is used for shut-down in the ERESET case (where a hard reboot is wanted).
 */

static void do_brutal_shutdown(void)
{
	/* Without 'MSG_TO_SYSLOG' this closes syslog. */
	open_logging(NULL, MSG_TO_STDERR);

	/* Grace time for our last syslog message to (hopefully) be written. */
	safe_sleep(1);

	/* Now stop all processes in their tracks. */
	log_message(LOG_INFO, "stopping all processes");
	kill(-1, SIGSTOP);
	keep_alive();

	/*
	 * Make sure we don't claim to be running after the planned reboot. As we have stopped
	 * all other process we don't need to worry about a 2nd daemon starting up. If this blocks
	 * (or next sync call() due to HDD locked up, etc) then the watchdog hardware should kick
	 * in and do the reset anyway.
	 */
	remove_pid_file();

	/*
	 * Try to save the file system's integrity prior to reboot. Won't be properly clean as
	 * stopped processes' files are still open, etc, but hopefully we get consistent directory
	 * entries, etc.
	 *
	 * The current 'man' page for sync has:
	 *
	 *		BUGS
	 *			According  to  the  standard specification (e.g., POSIX.1-2001), sync()
	 *			schedules the writes, but may return before the actual writing is done.
	 *			However,  since  version  1.3.20 Linux does actually wait.  (This still
	 *			does not guarantee data integrity: modern disks have large caches.)
	 *
	 * Hence we should be safe to reboot once a call to sync() returns, but as we may be
	 * using a battery-backed RAID card that might lie about the flush being done, we try
	 * sleeping then a 2nd sync() and off to reboot.
	 */
	log_message(LOG_INFO, "syncing file system");
	sync();
	safe_sleep(1);
	sync();
}


/*
 * This function performs the 'nice' shut-down for everything except ERESET.
 */

static void do_orderly_shutdown(int errorcode)
{
	/*
	 * In some cases we have little chance of proceeding unless we can free up
	 * resources. Start by assuming this was a user-space fault, and kill those
	 * processes with SIGTERM only. This should avoid killing syslog and other
	 * system-level processes we might want for sending email.
	 */

	kill_everything_else(FALSE, 1);

	/* if we will halt the system we should try to tell a sysadmin */
	if (admin != NULL) {
		run_func_as_child(60, send_email, errorcode, NULL);
	}

	open_logging(NULL, MSG_TO_STDERR); /* Without 'MSG_TO_SYSLOG' this closes syslog. */
	safe_sleep(1);		/* make sure log is written (send_email now has its own wait). */

	/* We cannot start shutdown, since init might not be able to fork. */
	/* That would stop the reboot process. So we try rebooting the system */
	/* ourselves. Note, that it is very likely we cannot start any rc */
	/* script either, so we do it all here. */

	/* Kill all processes. */
	kill_everything_else(TRUE, sigterm_delay-1);

	/*
	 * This will probably fail, as syslog daemon should be stopped now, but worth
	 * trying for debug/user privilege tests.
	 */
	open_logging(NULL, MSG_TO_STDERR | MSG_TO_SYSLOG);

	/*
	 * Make sure we don't claim to be running after the planned exit. As we have killed
	 * all other process we don't need to worry about a 2nd daemon starting up.
	 */
	remove_pid_file();

	/* Record the fact that we're going down */
	write_wtmp();

	/* Save the random seed. */
	save_urandom();

	/* Turn off accounting */
	if (acct(NULL) < 0)
		log_message(LOG_ERR, "failed stopping acct() (%s)", strerror(errno));

	/* Try system-supplied programs. */
	run_shutdown_children();

	/* In case the child process execution of umount failed, try any remaining ourselves. */
	unmount_disks_ourselves();
}
