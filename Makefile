RM      = rm
INSTALL = install
PREFIX  = /usr/local
CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -ldl -lrt -lm -fPIC


timescaler.so: timescaler.c Makefile
	$(CC) $(CFLAGS) timescaler.c -o timescaler.so -shared $(LDFLAGS)

clean:
	$(RM) -f timescaler.so
	$(MAKE) -C tests clean

distclean: clean
	$(MAKE) -C tests distclean

install: timescaler.so
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) timescaler.so $(PREFIX)/lib

uninstall:
	$(RM) $(PREFIX)/lib/timescaler.so

check: timescaler.so
	$(MAKE) -C tests check

.PHONY: clean install uninstall check
