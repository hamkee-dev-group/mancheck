# specdb/Makefile

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
LDFLAGS ?=
LIBS    ?= `pkg-config --cflags --libs sqlite3 2>/dev/null || echo -lsqlite3`

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
INCDIR  ?= $(PREFIX)/include
LIBDIR  ?= $(PREFIX)/lib

SRC     = src/specdb.c src/man2db.c
OBJ     = $(SRC:.c=.o)

all: specdb-build libspecdb.a

specdb-build: src/specdb.o src/man2db.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(LDFLAGS)

libspecdb.a: src/specdb.o
	@rm -f $@
	ar rcs $@ $^

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(INCDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp specdb-build $(DESTDIR)$(BINDIR)/
	cp include/specdb.h $(DESTDIR)$(INCDIR)/
	cp libspecdb.a $(DESTDIR)$(LIBDIR)/

clean:
	rm -f specdb-build libspecdb.a $(OBJ)

.PHONY: all install clean
