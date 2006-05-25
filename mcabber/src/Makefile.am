bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c roster.c roster.h events.c events.h \
		  jabglue.c jabglue.h jab_iq.c jab_priv.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h \
		  histolog.c histolog.h utils.c utils.h help.c help.h

LDADD = $(GLIB_LIBS) -lncurses -lpanel \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

INCLUDES = $(GLIB_CFLAGS)

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
