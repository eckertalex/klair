TARGET        = klair
CC            = cc
CFLAGS        = -std=c11 -Wall -Wextra -Wpedantic
DEBUG_FLAGS   = -Og -g -fsanitize=address -fsanitize=undefined
RELEASE_FLAGS = -O2 -DNDEBUG
INSTALL_DIR  ?= $(HOME)/.local/bin

VERSION    = $(shell git describe --tags --always 2>/dev/null || echo v0.0.0)
VERSIONDEF = -DKLAIR_VERSION='"$(VERSION)"'

all: release

release: $(TARGET)

$(TARGET): klair.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) $(VERSIONDEF) -o $@ klair.c

debug: klair.c
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(VERSIONDEF) -o $(TARGET)_debug klair.c

install: release
	mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/$(TARGET)

uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET)_debug *.dSYM

.PHONY: all release debug install uninstall clean
