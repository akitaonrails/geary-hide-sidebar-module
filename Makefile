# geary-hide-sidebar — GTK3 module build
#
#   make           build libgeary-hide-sidebar.so
#   make run       build, then launch geary with the module loaded
#   make debug     build, then launch geary with verbose module logging
#   make test      build and run the unit tests (headless via xvfb-run)
#   make install   copy the .so into ~/.local/lib/geary-hacks (optional)
#   make clean     remove build artifacts

MODULE   := libgeary-hide-sidebar.so
SRC      := geary-hide-sidebar.c
TEST_BIN := test_geary_hide_sidebar
TEST_SRC := test_geary_hide_sidebar.c
PREFIX   := $(HOME)/.local/lib/geary-hacks

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gmodule-2.0)
GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 gmodule-2.0)

ABS_MODULE := $(CURDIR)/$(MODULE)

.PHONY: all run debug test check install uninstall clean

all: $(MODULE)

$(MODULE): $(SRC)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -shared -o $@ $< $(GTK_LIBS)
	@echo "Built $(ABS_MODULE)"

# The test binary #includes the module source, so it depends on $(SRC) too.
$(TEST_BIN): $(TEST_SRC) $(SRC)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $(TEST_SRC) $(GTK_LIBS)

# Run headless under xvfb when available; the suite exits 77 (skipped) if it
# still can't reach a display.
test check: $(TEST_BIN)
	@if command -v xvfb-run >/dev/null 2>&1; then \
		xvfb-run -a ./$(TEST_BIN); \
	else \
		./$(TEST_BIN); \
	fi

run: $(MODULE)
	GTK_MODULES=$(ABS_MODULE) geary

debug: $(MODULE)
	GEARY_HIDE_SIDEBAR_DEBUG=1 GTK_MODULES=$(ABS_MODULE) geary

install: $(MODULE)
	mkdir -p $(PREFIX)
	cp -f $(MODULE) $(PREFIX)/$(MODULE)
	@echo "Installed to $(PREFIX)/$(MODULE)"
	@echo "Launch with: GTK_MODULES=$(PREFIX)/$(MODULE) geary"

uninstall:
	rm -f $(PREFIX)/$(MODULE)

clean:
	rm -f $(MODULE) $(TEST_BIN)
