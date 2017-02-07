/*************************************************************/
/* Original version was an example in the kernel source tree */
/*                                                           */
/* Most of the rest was written by me, Michael Meskes        */
/* meskes@debian.org                                         */
/*                                                           */
/*************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>		/* For EXEC_PAGESIZE */

#include "watch_err.h"
#include "extern.h"
#include "read-conf.h"		/* For add_list() */
#include "timefunc.h"

static int no_act = FALSE;

static void usage(char *progname)
{
	fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
	fprintf(stderr, "%s [options]\n", progname);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  -c | --config-file <file>  specify location of config file\n");
	fprintf(stderr, "  -f | --force               don't sanity-check config or use PID file\n");
	fprintf(stderr, "  -F | --foreground          run in foreground\n");
	fprintf(stderr, "  -X | --loop-exit <number>  run a fixed number of loops then exit\n");
	fprintf(stderr, "  -q | --no-action           do not reboot or halt\n");
	fprintf(stderr, "  -b | --softboot            soft-boot on error\n");
	fprintf(stderr, "  -s | --sync                sync filesystem\n");
	fprintf(stderr, "  -v | --verbose             verbose messages\n");
	exit(1);
}

/* Try to sync */
static int sync_system(int sync_it)
{
	if (sync_it) {
		sync();
		sync();
	}
	return (0);
}

/* execute repair binary */
static int repair(char *rbinary, int result, char *name, int version)
{
	char *arg[6];
	char parm[22];
	int ret;

	sprintf(parm, "%d", result);

	if (version == 0) {
		arg[0] = rbinary;	/* Use common repair binary with V0 test scripts, etc. */
		arg[1] = rbinary;
		arg[2] = parm;
		arg[3] = name;	/* May be null, not a problem here. */
		arg[4] = NULL;
		arg[5] = NULL;
	} else {
		arg[0] = name;	/* With V1 the test binary is also the repair binary. */
		arg[1] = name;
		arg[2] = "repair";
		arg[3] = parm;
		arg[4] = name;
		arg[5] = NULL;
	}

	/* no binary given, we have to reboot */
	if (arg[0] == NULL)
		return (result);

	ret = run_func_as_child(repair_timeout, exec_as_func, FLAG_REOPEN_STD_REPAIR, arg);

	/* check result */
	if (ret != 0) {
		log_message(LOG_ERR, "repair binary %s returned %d = '%s'", rbinary, ret, wd_strerror(ret));
	}

	return (ret);
}

static void wd_action(int result, char *rbinary, struct list *act)
{
	int version = 0;
	char *name = NULL;
	int timeout = TRUE;

	/* If we have info about the version, use this to decide what to call
	 * in order to repair the problem. Default is we use the global repair
	 * call, but for V1 we use the same program with a different argument.
	 */
	if (act != NULL) {
		name = act->name;
		version = act->version;
	}

	if (version == 1) {
		rbinary = name;
	}

	/* Decide on repair or return based on error code. */
	switch (result) {
	case ENOERR:
		/* No error, reset any time-out. */
		if (act != NULL) {
			act->last_time = 0;
			act->repair_count = 0;
		}
	case EDONTKNOW:
		/* Don't know, keep on working */
		return;

	case EREBOOT:
	case ERESET:
	case ETOOHOT:
	case EMAXLOAD:	/* System too busy? */
	case EMFILE:	/* "Too many open files" */
	case ENFILE:	/* "Too many open files in system" */
	case ENOMEM:	/* "Not enough space" */
		/* These are not repairable. */
		break;

	default:
		/* error that might be repairable */
		if (act != NULL && retry_timeout > 0) {
			/* timer possible and used to allow re-try */
			time_t now = time_mono(NULL);
			timeout = FALSE;

			if (act->last_time == 0) {
				/* First offence, record time. */
				act->last_time = now;
			} else {
				/* timer running */
				int tused = (int)(now - act->last_time);

				if (tused > retry_timeout) {
					log_message(LOG_WARNING, "Retry timed-out at %d seconds for %s", tused,
						act->name);
					timeout = TRUE;
				} else {
					if (verbose)
						log_message(LOG_DEBUG, "Retry at %d seconds for %s", tused, act->name);
				}
			}
		}

		if (timeout) {
			int try_repair = TRUE;
			/* check for too many failed repair attempts */
			if (act != NULL && repair_max > 0) {
				if (++act->repair_count > repair_max) {
					try_repair = FALSE;
					log_message(LOG_WARNING, "Repair count exceeded (%d for %s)",
						act->repair_count, act->name);
				} else {
					/* going to repair, reset re-try timer so same period for next try */
					act->last_time = 0;
					if (verbose) {
						log_message(LOG_DEBUG, "Repair attempt %d for %s",
							act->repair_count, act->name);
					}
				}
			}

			if (try_repair) {
				result = repair(rbinary, result, name, version);
			}
		} else {
			result = ENOERR;
		}
		break;
	}

	/* if still error, consider reboot */
	if (result != ENOERR) {
		/* if no-action flag set, do nothing */
		if (no_act) {
			if (verbose) {
				log_message(LOG_DEBUG, "Shutdown blocked by --no-action (error %d = '%s')",
					result, wd_strerror(result));
			}
		} else {
			do_shutdown(result);
		}
	}
}

static void do_check(int res, char *rbinary, struct list *act)
{
	wd_action(res, rbinary, act);
	wd_action(keep_alive(), rbinary, NULL);
}

static void old_option(int c, char *configfile)
{
	fprintf(stderr, "Option -%c is no longer valid, please specify it in %s.\n", c, configfile);
}

static void print_info(int sync_it, int force)
{
	struct list *act;

	log_message(LOG_INFO, "int=%ds realtime=%s sync=%s load=%d,%d,%d",
		    tint,
		    realtime ? "yes" : "no",
		    sync_it ? "yes" : "no",
		    maxload1, maxload5, maxload15);

	if (minpages == 0 && minalloc == 0)
		log_message(LOG_INFO, "memory not checked");
	else
		log_message(LOG_INFO, "memory: minimum pages = %d free, %d allocatable (%d byte pages)",
			minpages, minalloc, EXEC_PAGESIZE);

	if (target_list == NULL)
		log_message(LOG_INFO, "ping: no machine to check");
	else
		for (act = target_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "ping: %s", act->name);

	if (file_list == NULL)
		log_message(LOG_INFO, "file: no file to check");
	else
		for (act = file_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "file: %s:%d", act->name, act->parameter.file.mtime);

	if (pidfile_list == NULL)
		log_message(LOG_INFO, "pidfile: no server process to check");
	else
		for (act = pidfile_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "pidfile: %s", act->name);

	if (iface_list == NULL)
		log_message(LOG_INFO, "interface: no interface to check");
	else
		for (act = iface_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "interface: %s", act->name);

	if (temp_list == NULL)
		log_message(LOG_INFO, "temperature: no sensors to check");
	else {
		log_message(LOG_INFO, "temperature: maximum = %d", maxtemp);
		for (act = temp_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "temperature: %s", act->name);
	}

	if (tr_bin_list == NULL)
		log_message(LOG_INFO, "no test binary files");
	else {
		log_message(LOG_INFO, "test binary time-out = %d", test_timeout);
		for (act = tr_bin_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "%s: %s",
				act->version == 0 ? "test binary V0" : "test/repair V1",
				act->name);
	}

	if (repair_bin == NULL)
		log_message(LOG_INFO, "no repair binary files");
	else {
		log_message(LOG_INFO, "repair binary: time-out = %d", repair_timeout);
		log_message(LOG_INFO, "repair binary: program = %s", repair_bin);
	}

	log_message(LOG_INFO, "error retry time-out = %d seconds", retry_timeout);

	if (repair_max > 0) {
		log_message(LOG_INFO, "repair attempts = %d", repair_max);
	} else {
		log_message(LOG_INFO, "repair attempts = unlimited");
	}

	log_message(LOG_INFO, "alive=%s heartbeat=%s to=%s no_act=%s force=%s",
		    (devname == NULL) ? "[none]" : devname,
		    (heartbeat == NULL) ? "[none]" : heartbeat,
		    (admin == NULL) ? "[none]" : admin,
		    (no_act == TRUE) ? "yes" : "no",
		    (force == TRUE) ? "yes" : "no");
}

/*
 * Check some of the configured parameters for sensibility. If any look bad, exit with
 * the message about using the --force option to skip these checks.
 */

static void check_parameters(void)
{
	int err = 0;
	int min_timeout = 1;
	int max_timeout = 15 * dev_timeout;
	int temp_limit = 55;	/* Lower limit on temperature in Celsius. */

	if (max_timeout < 120) {
		max_timeout = 120;
	}

	/* check polling interval against the hardware timer value - VERY important. */
	if (dev_timeout - tint < 2) {
		log_message(LOG_ERR,
			    "Error: This interval length (%d) might reboot the system while the process sleeps!",
			    tint);
		err = 1;
	}

	/* check the 3 load average limits. */
	if (maxload1 > 0 && maxload1 < MINLOAD) {
		log_message(LOG_ERR,
			    "Error: Using this maximal 1-minute load average (%d) might reboot the system too often!",
			    maxload1);
		err = 1;
	}

	if (maxload5 > 0 && maxload5 < MINLOAD) {
		log_message(LOG_ERR,
			    "Error: Using this maximal 5-minute load average (%d) might reboot the system too often!",
			    maxload5);
		err = 1;
	}

	if (maxload15 > 0 && maxload15 < MINLOAD) {
		log_message(LOG_ERR,
			    "Error: Using this maximal 15-minute load average (%d) might reboot the system too often!",
			    maxload15);
		err = 1;
	}

	/* check the other time-outs, these can be independent of the hardware timer, but apply some sense! */
	if (repair_timeout > max_timeout || repair_timeout < min_timeout) {
		log_message(LOG_ERR,
			    "Error: This repair time-out (%d) looks out of a sensible range (%d..%d)!",
			    repair_timeout, min_timeout, max_timeout);
		err = 1;
	}

	if (test_timeout > max_timeout || test_timeout < min_timeout) {
		log_message(LOG_ERR,
			    "Error: This test time-out (%d) looks out of a sensible range (%d..%d)!",
			    test_timeout, min_timeout, max_timeout);
		err = 1;
	}

	/* unlike the other time-outs, setting retry time-out to zero is not totally unreasonable. */
	min_timeout = 0;
	if (retry_timeout > max_timeout || retry_timeout < min_timeout) {
		log_message(LOG_ERR,
			    "Error: This retry time-out (%d) looks out of a sensible range (%d..%d)!",
			    retry_timeout, min_timeout, max_timeout);
		err = 1;
	}

	if (temp_list != NULL && maxtemp < temp_limit) {
		log_message(LOG_ERR,
			    "Error: Max temperature of %d is too low to be sensible (test = %dC)",
			    maxtemp, temp_limit);
		err = 1;
	}

	if (err) {
		fatal_error(EX_USAGE, "To force parameter(s) use the --force command line option.");
	}
}

int main(int argc, char *const argv[])
{
	int c, foreground = FALSE, force = FALSE, sync_it = FALSE;
	char *configfile = CONFIG_FILENAME;
	struct list *act;
	char *progname;
	char *opts = "d:i:n:Ffsvbql:p:t:c:r:m:a:X:";
	struct option long_options[] = {
		{"config-file", required_argument, NULL, 'c'},
		{"foreground", no_argument, NULL, 'F'},
		{"force", no_argument, NULL, 'f'},
		{"sync", no_argument, NULL, 's'},
		{"no-action", no_argument, NULL, 'q'},
		{"verbose", no_argument, NULL, 'v'},
		{"softboot", no_argument, NULL, 'b'},
		{"loop-exit", required_argument, NULL, 'X'},
		{NULL, 0, NULL, 0}
	};
	long count = 0L;
	long count_max = 0L;
	unsigned long swait, twait;
	int softboot = FALSE;
	struct list *memtimer = NULL;
	struct list *loadtimer = NULL;

	progname = basename(argv[0]);
	open_logging(progname, MSG_TO_STDERR | MSG_TO_SYSLOG);

	/* check the options */
	/* there aren't that many any more */
	while ((c = getopt_long(argc, argv, opts, long_options, NULL)) != EOF) {
		switch (c) {
		case 'n':
		case 'p':
		case 'a':
		case 'r':
		case 'd':
		case 't':
		case 'l':
		case 'm':
		case 'i':
			old_option(c, configfile);
			usage(progname);
			break;
		case 'c':
			configfile = optarg;
			break;
		case 'F':
			foreground = TRUE;
			break;
		case 'f':
			force = TRUE;
			break;
		case 's':
			sync_it = TRUE;
			break;
		case 'b':
			softboot = TRUE;
			break;
		case 'q':
			no_act = TRUE;
			break;
		case 'v':
			verbose++;
			break;
		case 'X':
			count_max = atol(optarg);
			log_message(LOG_WARNING, "NOTE: Using --loop-exit so daemon will exit after %ld time intervals",
				    count_max);
			break;
		default:
			usage(progname);
		}
	}

	add_list(&memtimer, "<free-memory>", 0);
	add_list(&loadtimer, "<load-average>", 0);

	read_config(configfile);

	if (softboot) {
		/* Result of zeroing time-out is immediate action to shut down on errors, rather like old softboot behaviour. */
		retry_timeout = 0;
	}

	if (!force) {
		check_parameters();
	}

	/* make sure we get our own log directory */
	if (mkdir(logdir, 0750) && errno != EEXIST) {
		fatal_error(EX_SYSERR, "Cannot create directory %s (%s)", logdir, strerror(errno));
	}

	/* set up pinging if in ping mode */
	if (target_list != NULL) {
		open_netcheck(target_list);
	}

	if (!foreground) {
		/* allocate some memory to store a filename, this is needed later on even
		 * if the system runs out of memory */
		set_reopen_dir(logdir);

		if (wd_daemon(0, 0)) {
			fatal_error(EX_SYSERR, "failed to daemonize (%s)", strerror(errno));
		}
		open_logging(NULL, MSG_TO_SYSLOG);	/* Close terminal output, keep syslog open. */
	}

	/* tuck my process id away */
	if (!force && write_pid_file(PIDFILE)) {
		fatal_error(EX_USAGE, "unable to gain lock via PID file");
	}

	/* Log the starting message */
	log_message(LOG_NOTICE, "starting daemon (%d.%d):", MAJOR_VERSION, MINOR_VERSION);
	print_info(sync_it, force);

	/* open the device */
	if (no_act == FALSE) {
		open_watchdog(devname, dev_timeout);
	}

	open_tempcheck(temp_list);

	open_heartbeat();

	open_loadcheck();

	open_memcheck();

	/* set signal term to set our run flag to 0 so that */
	/* we make sure watchdog device is closed when receiving SIGTERM */
	signal(SIGTERM, sigterm_handler);

	lock_our_memory(realtime, schedprio, daemon_pid);

	/* Short wait (50ms OK?) in case test binaries return quickly, then
	 * remaining 'twait' should make watchdog sleep 'tint' seconds total.
	 */
	swait = 50000;
	twait = (tint * 1000000) - swait;

	/* main loop: update after <tint> seconds */
	while (_running) {
		wd_action(keep_alive(), repair_bin, NULL);

		/* sync system if we have to */
		do_check(sync_system(sync_it), repair_bin, NULL);

		/* check file table */
		do_check(check_file_table(), repair_bin, NULL);

		/* check load average */
		do_check(check_load(), repair_bin, loadtimer);

		/* check free memory */
		do_check(check_memory(), repair_bin, memtimer);

		/* check allocatable memory */
		do_check(check_allocatable(), repair_bin, memtimer);

		/* check temperature */
		for (act = temp_list; act != NULL; act = act->next)
			do_check(check_temp(act), repair_bin, act);

		/* in filemode stat file */
		for (act = file_list; act != NULL; act = act->next)
			do_check(check_file_stat_safe(act), repair_bin, act);

		/* in pidmode kill -0 processes */
		for (act = pidfile_list; act != NULL; act = act->next)
			do_check(check_pidfile(act), repair_bin, act);

		/* in network mode check the given devices for input */
		for (act = iface_list; act != NULL; act = act->next)
			do_check(check_iface(act), repair_bin, act);

		/* in ping mode ping the ip address */
		for (act = target_list; act != NULL; act = act->next)
			do_check(check_net(act->name,
					   act->parameter.net.sock_fp,
					   act->parameter.net.to,
					   act->parameter.net.packet, tint, pingcount), repair_bin, act);

		/* test, or test/repair binaries in the watchdog.d directory */
		for (act = tr_bin_list; act != NULL; act = act->next)
			do_check(check_bin(act->name, test_timeout, act->version), repair_bin, act);

		/* in case test binaries return quickly */
		usleep(swait);
		check_bin(NULL, test_timeout, 0);

		/* finally sleep for a full cycle */
		/* we have just triggered the device with the last check */
		usleep(twait);

		count++;

		/* do verbose logging */
		if (verbose && logtick && (--ticker == 0)) {
			ticker = logtick;
			log_message(LOG_DEBUG, "still alive after %ld interval(s)", count);
		}

		if (count_max > 0 && count >= count_max) {
			log_message(LOG_WARNING, "loop exit on interval counter reached");
			_running = 0;
		}
	}

	free_list(&loadtimer);
	free_list(&memtimer);

	terminate(EXIT_SUCCESS);
	/* not reached */
	return (EXIT_SUCCESS);
}
