RM      = rm
INSTALL = install
PREFIX  = /usr/local
CC      = gcc
CFLAGS  = -Wall -O2
LDFLAGS = -ldl -fPIC


all: timescaler.c
	$(CC) $(CFLAGS) timescaler.c -o timescaler.so -shared $(LDFLAGS)

clean:
	$(RM) -f timescaler.so

install: all
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) timescaler.so $(PREFIX)/lib

uninstall:
	$(RM) $(PREFIX)/lib/timescaler.so

.PHONY: all clean install uninstall
