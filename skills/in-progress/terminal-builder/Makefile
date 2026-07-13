CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local

baukasten: baukasten.c
	$(CC) $(CFLAGS) -o $@ $<

install: baukasten
	install -m 755 baukasten $(PREFIX)/bin/baukasten

clean:
	rm -f baukasten

.PHONY: install clean
