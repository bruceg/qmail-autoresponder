PACKAGE = qmail-autoresponder
VERSION = 0.90

CC = gcc
CFLAGS = -Wall -W -O0 -g

LD = gcc
LDFLAGS = -g
LIBS =

SOURCES = qmail-autoresponder.c

qmail-autoresponder: qmail-autoresponder.o

test: qmail-autoresponder runtests.sh
	bash runtests.sh
