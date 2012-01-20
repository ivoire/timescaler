RM      = rm
INSTALL = install
PREFIX  = /usr/local
CC      = gcc
CFLAGS  = -Wall -O2
LDFLAGS = -ldl -fPIC


all: timescaler.so

timescaler.so: timescaler.c Makefile
	$(CC) $(CFLAGS) timescaler.c -o timescaler.so -shared $(LDFLAGS)

clean:
	$(RM) -f timescaler.so

install: timescaler.so
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) timescaler.so $(PREFIX)/lib

uninstall:
	$(RM) $(PREFIX)/lib/timescaler.so

.PHONY: clean install uninstall
