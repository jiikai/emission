OS := $(shell uname -s)
CC := gcc
EXT := c
BINDIR := bin
SRCDIR := src
LOGDIR := log
LIBDIR := lib
TESTDIR := test
PREFIX := /usr/local
INCLUDE := -I$(PREFIX)/include -I/usr/include/postgresql -Isrc -Iinclude -Iinclude/dep
LIBINCLUDE := -L$(PREFIX)/lib -L/postgresql
STD := -std=c11 -pedantic
STACK := -fstack-protector -Wstack-protector
# ^^or: -fstack-protector-all (extra protection)
WARNS := -Wall -Wextra
DEBUG := -g
CFLAGS := -pthread -rdynamic $(INCLUDE) $(STD) $(STACK) $(WARNS) $(OPTFLAGS)
LIBS := -g -ldl -lrt -lpthread -lpq -lcrypto -lpcre -lcurl -lm $(LIBINCLUDE) $(OPTLIBS)
TESTLIBS := $(LIBS) -lcurl -L/curl
SRCS := $(wildcard $(SRCDIR)/**/*.c $(SRCDIR)/*.c)
TEST_SRCS= $(wildcard $(TESTDIR)/*_test.c)
OBJECTS :=$(patsubst %.c,$(LIBDIR)/%.o,$(SRCS))
TEST_OBJECTS :=$(patsubst %.c,%.o,$(TEST_SRCS))
MAIN_BINS := $(BINDIR)/emiss
TEST_BINS := $(patsubst %.c,%,$(TEST_SRCS))

# RULES

.PHONY: all valgrind tests clean

default: all

all: $(BINDIR)
	$(CC) $(CFLAGS) -O2 src/dep/civetweb.c src/dep/zip.c src/dep/bstrlib.c src/dep/libcsv.c src/wlcsv.c src/wlpq.c src/emiss_update.c src/emiss_retrieve.c src/emiss_resource.c src/emiss_server.c src/emiss.c -o $(MAIN_BINS) $(LIBS)

$(BINDIR):
	mkdir $@

valgrind: $(BINDIR)
	valgrind \
		--track-origins=yes \
		--leak-check=full \
		--leak-resolution=high \
		--show-leak-kinds=all \
		--log-file=$(LOGDIR)/$@-valgrind.log \
		$(BINDIR)/$(BINARY)
	@echo -en "\n- - - Log file: $(LOGDIR)/$@-valgrind.log - - -\n"


# Compile tests and run the test binary

tests:
	$(CC) $(CFLAGS) -I/curl $(DEBUG) $(TEST_SRCS) -o $(TEST_BINS) $(TESTLIBS)
	@sh ./$(TESTDIR)/runtests.sh

# Rule for cleaning the project
clean:
	@rm -rvf $(BINDIR)/* $(LOGDIR)/* $(TESTDIR)/vgcore.*;
	@find . -name "*.gc*" -exec rm {} \;
ifeq ($(OS),Darwin)
	@rm -rf `find . -name "*.dSYM" -print`
endif
