PACKAGE = qmail-autoresponder
VERSION = 0.94

CC = gcc
CFLAGS = -Wall -W -O0 -g

LD = gcc
LDFLAGS = -g
LIBS =

RM = rm -f

install_prefix =
prefix = $(install_prefix)/usr
bindir = $(prefix)/bin

install = /usr/bin/install

PROGS = qmail-autoresponder
SCRIPTS = vautoresponder

all: $(PROGS)

qmail-autoresponder: qmail-autoresponder.o
	$(LD) $(LDFLAGS) -o qmail-autoresponder qmail-autoresponder.o $(LIBS)

qmail-autoresponder.o: qmail-autoresponder.c

install:
	$(install) -d $(bindir)
	$(install) -m 755 $(PROGS) $(SCRIPTS) $(bindir)

test: qmail-autoresponder runtests.sh
	bash runtests.sh

clean:
	$(RM) *.o $(PROGS)
