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
#	GNU99=1		use GNU99 extensions
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
ifndef CC
CC = gcc
endif
CFLAGS = -Wall -W -pedantic
LD = $(CC)
LDLIBS = -lncurses -lpanel -lssl -L../libjabber -L../connwrap -llibjabber -lconnwrap

ifeq ($(DEBUG),1)
CFLAGS += -O0 -g -DDEBUG=1
else
CFLAGS += -O2 -mcpu=$(CPU)
LDFLAGS = -s
endif

ifeq ($(GNU99),1)
CFLAGS += -std=gnu99 -D_GNU_SOURCE
endif

CP = cp -f

SOURCES = \
    main.c \
    commands.c \
    screen.c \
    utils.c \
    buddies.c \
    parsecfg.c \
    jabglue.c \
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
	$(CP) $(JCLIENT) /usr/local/bin/$(JCLIENT)

#dep: $(SOURCES)
#	makedepend -f- -Ylydialog -- $(CFLAGS) -- $(SOURCES) > depend

#-include depend
