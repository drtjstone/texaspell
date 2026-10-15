# TeXAspell
#
#   make            build the command-line checker (build/texaspell)
#   make test       run the tests
#   make service    (macOS) build build/TeXAspell.service
#   make install    (macOS) install the service and the command-line tool
#   make uninstall  (macOS) remove them

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -Wno-unused-parameter
LDLIBS  += -laspell

UNAME := $(shell uname -s)
ifneq ($(UNAME),Darwin)
CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
endif

# Homebrew's aspell lives outside the default search paths.
ASPELL_PREFIX ?= $(shell brew --prefix aspell 2>/dev/null)
ifneq ($(ASPELL_PREFIX),)
CPPFLAGS += -I$(ASPELL_PREFIX)/include
LDFLAGS  += -L$(ASPELL_PREFIX)/lib -Wl,-rpath,$(ASPELL_PREFIX)/lib
endif

CORE_SRC = src/core/strset.c src/core/scan.c src/core/cmddb.c \
           src/core/speller.c src/core/checker.c
CORE_HDR = $(wildcard src/core/*.h)

SERVICE   = build/TeXAspell.service
PREFIX   ?= $(HOME)/.local
SERVICES_DIR = $(HOME)/Library/Services

.PHONY: all test service install uninstall clean

all: build/texaspell

build/texaspell: $(CORE_SRC) $(CORE_HDR) src/cli/main.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SRC) src/cli/main.c $(LDFLAGS) $(LDLIBS)

test: build/texaspell
	sh tests/run.sh

ifeq ($(UNAME),Darwin)
service: $(SERVICE)

$(SERVICE): $(CORE_SRC) $(CORE_HDR) src/macos/main.m macos/Info.plist
	@rm -rf $@
	@mkdir -p $@/Contents/MacOS
	cp macos/Info.plist $@/Contents/Info.plist
	$(CC) $(CPPFLAGS) $(CFLAGS) -fobjc-arc -o $@/Contents/MacOS/TeXAspell \
		$(CORE_SRC) src/macos/main.m $(LDFLAGS) $(LDLIBS) -framework AppKit
	codesign --force --sign - $@

install: $(SERVICE) build/texaspell
	-pkill -x TeXAspell
	@mkdir -p $(SERVICES_DIR) $(PREFIX)/bin
	rm -rf $(SERVICES_DIR)/TeXAspell.service
	cp -R $(SERVICE) $(SERVICES_DIR)/
	cp build/texaspell $(PREFIX)/bin/
	/System/Library/CoreServices/pbs -update
	@echo
	@echo "Installed. Now choose \"British English (TeXAspell)\" in TeXShop's"
	@echo "Edit > Spelling and Grammar > Show Spelling and Grammar panel."
	@echo "If it is not listed, log out and back in."

uninstall:
	-pkill -x TeXAspell
	rm -rf $(SERVICES_DIR)/TeXAspell.service
	rm -f $(PREFIX)/bin/texaspell
	/System/Library/CoreServices/pbs -update
else
service install uninstall:
	@echo "The spell service can only be built on macOS."; exit 1
endif

clean:
	rm -rf build
