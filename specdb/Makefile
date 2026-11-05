# specdb/Makefile

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
CFLAGS  += -MMD -MP
LDFLAGS ?=
LIBS    ?= `pkg-config --cflags --libs sqlite3 2>/dev/null || echo -lsqlite3`

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
INCDIR  ?= $(PREFIX)/include
LIBDIR  ?= $(PREFIX)/lib

SRC     = src/specdb.c src/contracts.c src/man2db.c
BUILD_DIR = build
OBJ     = $(addprefix $(BUILD_DIR)/,$(SRC:.c=.o))

.DEFAULT_GOAL := all
-include $(OBJ:.o=.d)

all: specdb-build libspecdb.a

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

specdb-build: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

libspecdb.a: $(BUILD_DIR)/src/specdb.o $(BUILD_DIR)/src/contracts.o
	@rm -f $@
	ar rcs $@ $^

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(INCDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp specdb-build $(DESTDIR)$(BINDIR)/
	cp src/specdb.h $(DESTDIR)$(INCDIR)/
	cp libspecdb.a $(DESTDIR)$(LIBDIR)/

clean:
	rm -rf $(BUILD_DIR) specdb-build libspecdb.a

.PHONY: all install clean
