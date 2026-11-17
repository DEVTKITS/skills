# Makefile der C-Fassung (Track svgkit-c).
#
#   make            baut dsui-c (HTTP-Server) und dsui-svg (SVG-Kommandozeile)
#   make dsui-svg   nur die Kommandozeile  -> c/dsui-svg
#   make dsui-c     nur den Server         -> c/dsui-c
#   make asan       beide mit AddressSanitizer + UBSan -> $(OUTDIR)/dsui-svg-asan, $(OUTDIR)/dsui-c-asan
#                   (OUTDIR=Ordner, Standard: dieser Ordner)
#   make clean
#
# Warnungen sind Fehler (-Werror), damit nichts Unsauberes durchrutscht.
# -ffp-contract=off: der Übersetzer darf a*b+c nicht zu einem FMA zusammenziehen
# (sonst würde die Byte-Parität zu svgkit.js brechen). Kein -ffast-math. Kein -lm:
# svgkit.c bringt sin/cos (fdlibm-Port) und floor/ceil selbst mit.
CC      ?= cc
STD     := -std=c99 -Wall -Wextra -Wpedantic -Werror -ffp-contract=off
OPT     ?= -O2
CFLAGS  += $(STD) $(OPT)
OUTDIR  ?= .
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1

.PHONY: all clean asan
all: dsui-c dsui-svg

# server.c bindet svgkit.c direkt ein ("#include"), damit auch der alte Einzeiler
# "cc -O2 -std=c99 -pthread -o dsui-c server.c" (test.sh) weiterhin baut.
dsui-c: server.c svgkit.c svgkit.h
	$(CC) $(CFLAGS) -pthread -o $@ server.c

dsui-svg: svgkit_cli.c svgkit.c svgkit.h
	$(CC) $(CFLAGS) -o $@ svgkit_cli.c svgkit.c

asan: $(OUTDIR)/dsui-svg-asan $(OUTDIR)/dsui-c-asan

$(OUTDIR)/dsui-svg-asan: svgkit_cli.c svgkit.c svgkit.h
	$(CC) $(STD) $(SAN) -o $@ svgkit_cli.c svgkit.c

$(OUTDIR)/dsui-c-asan: server.c svgkit.c svgkit.h
	$(CC) $(STD) $(SAN) -pthread -o $@ server.c

clean:
	rm -f dsui-c dsui-svg dsui-svg-asan dsui-c-asan
