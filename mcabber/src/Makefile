#
# mcabber Makefile
#


#
#  Available options:
#
#    Environment variables:
#	CPU		optimize for the given processor.
#			default = pentium
#	DEBUG=1		disable optimizations and build for debug
#			default = no
#
#    Targets:
#	all:		build executable
#	clean:		remove object files
#	realclean:	remove all generated files
#	install:	build & install
#	dep:		create dependencies
#


.PHONY: all clean realclean install
# dep

CPU ?= i386
JCLIENT = mcabber

CC = gcc
CFLAGS = -Wall -W -pedantic
LD = gcc
LDLIBS = -lncurses -lpanel

ifeq ($(DEBUG),1)
CFLAGS += -O0 -g -DDEBUG=1
else
CFLAGS += -O2 -mcpu=$(CPU)
LDFLAGS = -s
endif

CP = cp -f

SOURCES = \
    main.c \
    screen.c \
    utils.c \
    buddies.c \
    parsecfg.c \
    server.c \
    socket.c \
    lang.c \
    utf8.c

OBJECTS = $(SOURCES:.c=.o)

.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<

all: $(JCLIENT)

$(JCLIENT): $(OBJECTS)
	$(LD) -o $@ $(LDFLAGS) $^ $(LDLIBS)

clean:
	-$(RM) *~
	-$(RM) $(JCLIENT)
	-$(RM) $(OBJECTS)
#	-$(RM) depend

realclean: clean
	-$(RM) $(JCLIENT)

install: all
	$(CP) $(JCLIENT) /usr/bin/$(JCLIENT)

#dep: $(SOURCES)
#	makedepend -f- -Ylydialog -- $(CFLAGS) -- $(SOURCES) > depend

#-include depend
