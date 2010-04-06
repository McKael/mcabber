bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c main.h roster.c roster.h events.c events.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h utf8.c utf8.h \
		  histolog.c histolog.h utils.c utils.h pgp.c pgp.h \
		  xmpp.c xmpp.h xmpp_helper.c xmpp_helper.h xmpp_defines.h \
		  xmpp_iq.c xmpp_iq.h xmpp_iqrequest.c xmpp_iqrequest.h \
		  xmpp_muc.c xmpp_muc.h xmpp_s10n.c xmpp_s10n.h \
		  caps.c caps.h fifo.c fifo.h help.c help.h

if OTR
mcabber_SOURCES += otr.c otr.h nohtml.c nohtml.h
endif

LDADD = $(GLIB_LIBS) $(LOUDMOUTH_LIBS) $(GPGME_LIBS) $(LIBOTR_LIBS) \
				$(ENCHANT_LIBS) $(LIBIDN_LIBS)

AM_CPPFLAGS = -I$(top_srcdir) $(GLIB_CFLAGS) $(LOUDMOUTH_CFLAGS) \
				$(GPGME_CFLAGS) $(LIBOTR_CFLAGS) \
				$(ENCHANT_CFLAGS) $(LIBIDN_CFLAGS)

CLEANFILES = hgcset.h

if HGCSET
BUILT_SOURCES = hgcset.h

hgcset.h:
	../hgcset.sh

.PHONY: hgcset.h
endif

if INSTALL_HEADERS
mcabber_SOURCES += modules.c modules.h api.h
mcabberinclude_HEADERS = main.h roster.h events.h \
			 commands.h compl.h \
			 hbuf.h screen.h logprint.h \
			 settings.h hooks.h utf8.c utf8.h \
			 histolog.h utils.h pgp.h \
			 xmpp.h xmpp_helper.h xmpp_defines.h \
			 xmpp_iq.h xmpp_iqrequest.h \
			 xmpp_muc.h xmpp_s10n.h \
			 caps.h fifo.h help.h modules.h api.h \
			 $(top_srcdir)/include/config.h

if OTR
mcabberinclude_HEADERS += otr.h nohtml.h
endif

if HGCSET
mcabberinclude_HEADERS += hgcset.h
endif

mcabberincludedir = $(includedir)/mcabber
endif

#SUBDIRS =
