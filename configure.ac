dnl Process this file with autoconf to produce a configure script.
AC_INIT(include/extern.h)

MAJOR_VERSION=6
MINOR_VERSION=0
AM_INIT_AUTOMAKE(watchdog, $MAJOR_VERSION.$MINOR_VERSION)

AC_PREFIX_DEFAULT(/usr)

AC_DEFINE_UNQUOTED(MAJOR_VERSION, $MAJOR_VERSION, "major version number")
AC_DEFINE_UNQUOTED(MINOR_VERSION, $MINOR_VERSION, "minor version number")

AM_CONFIG_HEADER(include/config.h)
AM_SANITY_CHECK
AM_MAINTAINER_MODE
	
dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL

AC_PATH_PROG(PATH_SENDMAIL, sendmail, /usr/lib/sendmail, $PATH /usr/bin /usr/sbin /usr/etc /etc /usr/ucblib)
if test -n "$PATH_SENDMAIL"; then
  AC_DEFINE_UNQUOTED(PATH_SENDMAIL, "$PATH_SENDMAIL", "path to sendmail binary")
fi

AC_PATH_PROG(PATH_HWCLOCK, hwclock, /sbin/hwclock, $PATH /usr/bin /usr/sbin /usr/etc /etc /usr/ucblib)
if test -n "$PATH_HWCLOCK"; then
  AC_DEFINE_UNQUOTED(PATH_HWCLOCK, "$PATH_HWCLOCK", "path to hwclock binary")
fi

AC_PATH_PROG(PATH_SWAPOFF, swapoff, /sbin/swapoff, $PATH /usr/bin /usr/sbin /usr/etc /etc /usr/ucblib)
if test -n "$PATH_SWAPOFF"; then
  AC_DEFINE_UNQUOTED(PATH_SWAPOFF, "$PATH_SWAPOFF", "path to swapoff binary")
fi

AC_PATH_PROG(PATH_UMOUNT, umount, /bin/umount, $PATH /usr/bin /usr/sbin /usr/etc /etc /usr/ucblib)
if test -n "$PATH_UMOUNT"; then
  AC_DEFINE_UNQUOTED(PATH_UMOUNT, "$PATH_UMOUNT", "path to umount binary")
fi

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(fcntl.h limits.h paths.h sys/ioctl.h sys/time.h syslog.h unistd.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_ST_RDEV
AC_HEADER_TIME

dnl Checks for library functions.
AC_FUNC_ALLOCA
AC_PROG_GCC_TRADITIONAL
AC_TYPE_SIGNAL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(gethostname select socket strcspn strdup strerror strstr strtoul uname)

AC_MSG_CHECKING(whether to log via syslog)
AC_ARG_ENABLE(syslog,
[  --disable-syslog        Disable logging by syslog (deprecated)],[
   if test "$enableval" = "yes"; then
      AC_DEFINE(USE_SYSLOG, 1, "enable syslog")
	  AC_MSG_RESULT(yes)
   else
      AC_DEFINE(USE_SYSLOG, 0, "enable syslog")
	  AC_MSG_RESULT(no)
   fi
],[
  AC_DEFINE(USE_SYSLOG, 1, "enable syslog")
  AC_MSG_RESULT(yes)
])

AC_MSG_CHECKING(whether to include NFS support)
AC_ARG_ENABLE(nfs,
[  --disable-nfs           Disable NFS support],[
   if test "$enableval" = "yes"; then
      AC_DEFINE(HAVE_NFS, 1, "enable NFS")
	  AC_MSG_RESULT(yes)
   else
      AC_DEFINE(HAVE_NFS, 0, "enable NFS")
	  AC_MSG_RESULT(no)
   fi
],[
  AC_DEFINE(HAVE_NFS, 1, "enable NFS")
  AC_MSG_RESULT(yes)
])

AC_MSG_CHECKING(for minimum value accepted as reboot cause)
AC_ARG_WITH(minload,
[  --with-minload=int      minimum value accepted as reboot cause (default 2)],[
  AC_DEFINE_UNQUOTED(MINLOAD, $withval, "minimum value accepted as reboot cause")
  AC_MSG_RESULT($withval)
],[
  AC_DEFINE(MINLOAD, 2, "minimum value accepted as reboot cause")
  AC_MSG_RESULT(2)
])

AC_MSG_CHECKING(for timer margin used by kernel)
AC_ARG_WITH(timermargin,
[  --with-timermargin=int  timer margin used by kernel (default 60)],[
  AC_DEFINE_UNQUOTED(TIMER_MARGIN, $withval, "timer margin used by kernel")
  AC_MSG_RESULT($withval)
],[
  AC_DEFINE(TIMER_MARGIN, 60, "timer margin used by kernel")
  AC_MSG_RESULT(60)
])

AC_MSG_CHECKING(for config filename)
AC_ARG_WITH(configfile,
[  --with-configfile=filename
			  name of config file (default /etc/watchdog.conf)],[
  AC_DEFINE_UNQUOTED(CONFIG_FILENAME, "$withval", "name of config file")
  CONFIG_FILENAME="$withval"
  AC_MSG_RESULT($withval)
],[
  CONFIG_FILENAME="/etc/watchdog.conf"
  AC_DEFINE_UNQUOTED(CONFIG_FILENAME, "$CONFIG_FILENAME", "name of config file")
  AC_MSG_RESULT($CONFIG_FILENAME)
])
AC_SUBST(CONFIG_FILENAME)

AC_MSG_CHECKING(for test binary directory)
AC_ARG_WITH(test-bin-path,
[  --with-test-bin-path=directory
			  path to test binaries (default /etc/watchdog.d)],[
  AC_DEFINE_UNQUOTED(TESTBIN_PATH, "$withval", "path to test binaries")
  TESTBIN_PATH="$withval"
  AC_MSG_RESULT($withval)
],[
  TESTBIN_PATH="/etc/watchdog.d"
  AC_DEFINE_UNQUOTED(TESTBIN_PATH, "$TESTBIN_PATH", "path to test binaries")
  AC_MSG_RESULT($TESTBIN_PATH)
])
AC_SUBST(TESTBIN_PATH)

AC_MSG_CHECKING(for pid filename)
AC_ARG_WITH(pidfile,
[  --with-pidfile=filename name of pid file (default /var/run/watchdog.pid)],[
  AC_DEFINE_UNQUOTED(PIDFILE, "$withval", "name of pid file")
  AC_MSG_RESULT($withval)
],[
  AC_DEFINE(PIDFILE, "/var/run/watchdog.pid", "name of pid file")
  AC_MSG_RESULT("/var/run/watchdog.pid")
])

AC_MSG_CHECKING(for keepalive pid filename)
AC_ARG_WITH(ka_pidfile,
[  --with-ka_pidfile=filename name of keepalive pid file (default /var/run/wd_keepalive.pid)],[
  AC_DEFINE_UNQUOTED(KA_PIDFILE, "$withval", "name of keepalive pid file")
  AC_MSG_RESULT($withval)
],[
  AC_DEFINE(KA_PIDFILE, "/var/run/wd_keepalive.pid", "name of keepalive pid file")
  AC_MSG_RESULT("/var/run/wd_keepalive.pid")
])

AC_MSG_CHECKING(for random seed filename)
AC_ARG_WITH(randomseed,
[  --with-randomseed=filename
                          filename for storing random seed (default /var/run/random-seed)],[
  AC_DEFINE_UNQUOTED(RANDOM_SEED, "$withval", filename for storing random seed")
  AC_MSG_RESULT($withval)
],[
  AC_DEFINE(RANDOM_SEED, "/var/run/random-seed", "filename for storing random seed")
  AC_MSG_RESULT("/var/run/random-seed")
])

AC_OUTPUT([Makefile src/Makefile])
