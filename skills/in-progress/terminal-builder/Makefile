CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local

# Unity-Build: src/serum.c bindet die Modul-Dateien (src/*.c) ein.
SRC     := src/serum.c
MODULES := $(wildcard src/0*.c) src/01_types.h

baukasten: $(SRC) $(MODULES)
	$(CC) $(CFLAGS) -o $@ $(SRC)

install: baukasten
	install -m 755 baukasten $(PREFIX)/bin/baukasten

clean:
	rm -f baukasten

.PHONY: install clean
