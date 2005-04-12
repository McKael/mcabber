bin_PROGRAMS = mcabber
mcabber_SOURCES = main.c jabglue.c jabglue.h commands.c commands.h \
		  buddies.c buddies.h screen.c screen.h \
		  parsecfg.c parsecfg.h utf8.c utf8.h \
		  utils.c utils.h lang.c lang.h list.h harddefines.h

LDADD = -lncurses -lpanel -lssl \
	../libjabber/liblibjabber.a ../connwrap/libconnwrap.a

#SUBDIRS =
#INCLUDES =

