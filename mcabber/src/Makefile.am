bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c roster.c roster.h events.c events.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h utf8.c utf8.h \
		  histolog.c histolog.h utils.c utils.h pgp.c pgp.h \
		  fifo.c fifo.h help.c help.h \
		  xmpp.c xmpp.h xmpp_helper.h

if OTR
mcabber_SOURCES += otr.c otr.h nohtml.c nohtml.h
endif

LDADD = $(GLIB_LIBS) $(LOUDMOUTH_LIBS) $(GPGME_LIBS) $(LIBOTR_LIBS) \
				$(ENCHANT_LIBS)

AM_CPPFLAGS = $(GLIB_CFLAGS) $(LOUDMOUTH_CFLAGS) $(GPGME_CFLAGS) \
				$(LIBOTR_CFLAGS) $(ENCHANT_CFLAGS)

CLEANFILES = hgcset.h

if HGCSET
BUILT_SOURCES = hgcset.h

hgcset.h:
	../hgcset.sh

.PHONY: hgcset.h
endif

#SUBDIRS =
