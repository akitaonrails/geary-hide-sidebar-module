# geary-hide-sidebar — GTK3 module build
#
#   make           build libgeary-hide-sidebar.so
#   make run       build, then launch geary with the module loaded
#   make debug     build, then launch geary with verbose module logging
#   make install   copy the .so into ~/.local/lib/geary-hacks (optional)
#   make clean     remove build artifacts

MODULE  := libgeary-hide-sidebar.so
SRC     := geary-hide-sidebar.c
PREFIX  := $(HOME)/.local/lib/geary-hacks

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gmodule-2.0)
GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 gmodule-2.0)

ABS_MODULE := $(CURDIR)/$(MODULE)

.PHONY: all run debug install uninstall clean

all: $(MODULE)

$(MODULE): $(SRC)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -shared -o $@ $< $(GTK_LIBS)
	@echo "Built $(ABS_MODULE)"

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
	rm -f $(MODULE)
