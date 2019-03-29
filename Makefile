OS := $(shell uname -s)
CC := gcc
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
WARNS := -Wall -Wextra
DEBUG := -g
CFLAGS := -O3 -g -pthread $(INCLUDE) $(STD) $(STACK) $(WARNS) $(OPTFLAGS)
LIBS := -ldl -lm -lpthread -lcurl -lpcre -lpq $(LIBINCLUDE) $(OPTLIBS)
TESTLIBS := $(LIBS)
SRCS := $(wildcard $(SRCDIR)/dep/*.c $(SRCDIR)/*.c)
TEST_SRCS= $(wildcard $(TESTDIR)/*_test.c)
OBJECTS := $(patsubst %.c,$(LIBDIR)/%.o,$(SRCS))
TEST_OBJECTS := $(patsubst %.c,%.o,$(TEST_SRCS))
MAIN_BINS := $(BINDIR)/emiss
TEST_BINS := $(patsubst %.c,%,$(TEST_SRCS))

# RULES

.PHONY: all valgrind tests clean

default: all

all: $(BINDIR)
	$(CC) $(CFLAGS) $(SRCS) -DNDEBUG -DHEROKU -o $(MAIN_BINS) $(LIBS)

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
