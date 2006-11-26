bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c roster.c roster.h events.c events.h \
		  jabglue.c jabglue.h jab_iq.c jab_priv.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h utf8.c utf8.h \
		  histolog.c histolog.h utils.c utils.h help.c help.h

LDADD = $(GLIB_LIBS) $(GPGME_LIBS) \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GPGME_CFLAGS)

CLEANFILES = hgcset.h

if HGCSET
BUILT_SOURCES = hgcset.h

#mcabber_DEPENDENCIES = hgcset.h \
#	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

hgcset.h:
	../hgcset.sh

.PHONY: hgcset.h
endif

#SUBDIRS =
