bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c jabglue.c jabglue.h roster.c roster.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h \
		  histolog.c histolog.h utils.c utils.h list.h

LDADD = $(GLIB_LIBS) -lncurses -lpanel \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

INCLUDES = $(GLIB_CFLAGS)

#SUBDIRS =

