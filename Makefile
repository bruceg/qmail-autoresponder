CC = gcc
CFLAGS = -Wall -W -O0 -g

LD = gcc
LDFLAGS = -g
LIBS =

autoresponder: autoresponder.o

test: autoresponder runtests.sh
	bash runtests.sh
