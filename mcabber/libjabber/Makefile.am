noinst_LIBRARIES = liblibjabber.a
EXTRA_DIST = jid.c log.c jutil.c str.c expat.c hashtable.c genhash.c pproxy.c pool.c rate.c xmlparse.c xstream.c xmltok.c xmlnode.c jconn.c xmlrole.c jpacket.c snprintf.c socket.c sha.c xmltok_impl_c.h latin1tab.h log.h asciitab.h libxode.h jabber.h hashtable.h xmlparse.h iasciitab.h xmldef.h xmltok.h utf8tab.h xmltok_impl.h xmlrole.h nametab.h xmltok_ns_c.h
AUTOMAKE_OPTIONS = foreign

if BUILD_JABBER

liblibjabber_a_SOURCES =  jid.c log.c jutil.c str.c expat.c hashtable.c genhash.c pproxy.c pool.c rate.c xmlparse.c xstream.c xmltok.c xmlnode.c jconn.c xmlrole.c jpacket.c snprintf.c socket.c sha.c

else

liblibjabber_a_LIBADD = dummy.o

dummy.c:
	touch dummy.c

endif

SUBDIRS = 
INCLUDES = -I$(top_srcdir)/../connwrap -I$(top_srcdir)/../connwrap-0.1
CPPFLAGS = 
