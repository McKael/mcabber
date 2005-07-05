bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c jabglue.c jabglue.h roster.c roster.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h \
		  settings.c settings.h parsecfg.c parsecfg.h \
		  hooks.c hooks.h histolog.c histolog.h \
		  utf8.c utf8.h utils.c utils.h list.h harddefines.h

LDADD = -lglib-2.0 -lncurses -lpanel \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

INCLUDES = $(GLIB_CFLAGS)

#SUBDIRS =

