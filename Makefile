# mancheck – top-level Makefile
#
# Builds specdb (library + specdb-build binary) then the analyzer.
# The analyzer links against libspecdb.a for manpage-driven rule lookup.
#
# Targets:
#   all      – build everything
#   test     – build then run the test suite
#   clean    – clean all build artifacts
#   specdb   – build specdb library and specdb-build binary only
#   analyzer – build analyzer only (requires specdb built first)

.PHONY: all specdb analyzer test clean

all: specdb analyzer

specdb:
	$(MAKE) -C specdb

analyzer: specdb
	$(MAKE) -C analyzer

test: all
	bash mc_tests/run.sh

clean:
	$(MAKE) -C specdb clean
	$(MAKE) -C analyzer clean
