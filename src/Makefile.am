sbin_PROGRAMS = watchdog wd_keepalive wd_identify

watchdog_SOURCES = watchdog.c configfile.c daemon-pid.c errorcodes.c \
			file_stat.c file_table.c heartbeat.c iface.c keep_alive.c \
			killall5.c load.c lock_mem.c logmessage.c memory.c net.c \
			pidfile.c read-conf.c reopenstd.c run-as-child.c send-email.c \
			shutdown.c temp.c test_binary.c xmalloc.c timefunc.c sigterm.c

wd_keepalive_SOURCES = wd_keepalive.c configfile.c logmessage.c read-conf.c xmalloc.c \
			daemon-pid.c lock_mem.c keep_alive.c sigterm.c

wd_identify_SOURCES = wd_identify.c configfile.c logmessage.c read-conf.c xmalloc.c

AM_CPPFLAGS = -I@top_srcdir@/include

LIBS = -lrt

distclean-depend:
	rm -rf .deps
