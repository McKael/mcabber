bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c jabglue.c jabglue.h commands.c commands.h \
		  roster.c roster.h hbuf.c hbuf.h screen.c screen.h \
		  parsecfg.c parsecfg.h utf8.c utf8.h \
		  utils.c utils.h lang.c lang.h list.h harddefines.h

LDADD = -lglib-2.0 -lncurses -lpanel -lssl \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

INCLUDES = $(GLIB_CFLAGS)

#SUBDIRS =

