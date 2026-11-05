CC      ?= clang
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -g
CFLAGS  += -Iinclude
CFLAGS  += -MMD -MP

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
    src/mc_flow.c \
    src/mc_rules.c \
    src/mc_db_integration.c \
    libdb/mc_db.c \
    src/report.c \
    src/mc_suppress.c \
    src/mc_compdb.c \
    src/mc_preproc.c \
    $(TS_LIB_SRC) \
    $(TS_C_SRCS)

# All objects live under build/; vendored sources stay untouched
# (../vendor/x.c -> build/vendor/x.o, src/x.c -> build/src/x.o).
BUILD_DIR = build
OBJS = $(addprefix $(BUILD_DIR)/,$(patsubst ../%,%,$(SRCS:.c=.o)))

.DEFAULT_GOAL := all
-include $(OBJS:.o=.d)

all: analyzer

$(BUILD_DIR)/vendor/%.o: ../vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

analyzer: $(OBJS) $(SPECDB_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(SPECDB_LIB) $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR) analyzer

.PHONY: all clean
