CC      ?= clang
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -g
CFLAGS  += -Iinclude

CFLAGS  += -I../vendor/tree-sitter/lib/include
CFLAGS  += -I../vendor/tree-sitter/lib/src
CFLAGS  += -I../vendor/tree-sitter-c/src
CFLAGS  += -include mc_ts_compat.h
CFLAGS += -Ilibdb

# specdb integration
CFLAGS  += -I../specdb/src
SPECDB_LIB = ../specdb/libspecdb.a

LDLIBS  = -lsqlite3

TS_LIB_SRC   = ../vendor/tree-sitter/lib/src/lib.c
TS_C_PARSER  = ../vendor/tree-sitter-c/src/parser.c
TS_C_SCANNER = ../vendor/tree-sitter-c/src/scanner.c

# Some grammars do NOT have scanner.c. Guard it if needed:
TS_C_SCANNER_EXISTS := $(wildcard $(TS_C_SCANNER))
TS_C_SRCS = $(TS_C_PARSER)
ifneq ($(TS_C_SCANNER_EXISTS),)
TS_C_SRCS += $(TS_C_SCANNER)
endif

SRCS = \
    src/main.c \
    src/mc_ts.c \
    src/mc_rules.c \
    src/mc_db_integration.c \
    libdb/mc_db.c \
    src/report.c \
    src/mc_suppress.c \
    src/mc_preproc.c \
    $(TS_LIB_SRC) \
    $(TS_C_SRCS)

OBJS = $(SRCS:.c=.o)

all: analyzer

analyzer: $(OBJS) $(SPECDB_LIB)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(SPECDB_LIB) $(LDLIBS)

clean:
	rm -f $(OBJS) analyzer

