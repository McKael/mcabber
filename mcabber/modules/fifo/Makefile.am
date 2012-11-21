
if INSTALL_HEADERS
pkglib_LTLIBRARIES = libfifo.la
libfifo_la_SOURCES = fifo_module.c $(top_srcdir)/mcabber/fifo.c $(top_srcdir)/mcabber/fifo.h
libfifo_la_LDFLAGS = -module -avoid-version -shared

LDADD = $(GLIB_LIBS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir) \
				$(GLIB_CFLAGS) $(LOUDMOUTH_CFLAGS) \
				$(GPGME_CFLAGS) $(LIBOTR_CFLAGS) \
				$(ENCHANT_CFLAGS)
endif

