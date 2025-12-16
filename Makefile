# Clixon VPP Control Plane - Makefile
#
# Build configuration for Clixon backend plugin

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -g -O2
LDFLAGS = -shared

# Package config for dependencies
PKG_CONFIG = pkg-config

# Clixon paths (adjust if needed)
CLIXON_CFLAGS := $(shell $(PKG_CONFIG) --cflags clixon 2>/dev/null || echo "-I/usr/local/include")
CLIXON_LIBS := $(shell $(PKG_CONFIG) --libs clixon 2>/dev/null || echo "-L/usr/local/lib -lclixon")

# VPP paths (adjust for VPP 25.06)
VPP_PREFIX ?= /usr
VPP_INCLUDE = $(VPP_PREFIX)/include
VPP_LIB = $(VPP_PREFIX)/lib/x86_64-linux-gnu

# VPP VAPI headers location
VAPI_INCLUDE = $(VPP_INCLUDE)/vapi

# VPP flags (disabled for stub mode)
# VPP_CFLAGS = -I$(VPP_INCLUDE) -I$(VAPI_INCLUDE)
# VPP_LIBS = -L$(VPP_LIB) -lvapiclient -lvlibmemoryclient -lsvm -lvppinfra
VPP_CFLAGS =
VPP_LIBS =

# All flags combined
ALL_CFLAGS = $(CFLAGS) $(CLIXON_CFLAGS) $(VPP_CFLAGS) -DVPP_VERSION=2506 -DVPP_STUB_MODE
ALL_LIBS = $(CLIXON_LIBS) $(VPP_LIBS) -lpthread

# Source files
SRCS = src/vpp_plugin.c \
       src/vpp_connection.c \
       src/vpp_interface.c

# Object files
OBJS = $(SRCS:.c=.o)

# Output plugin
PLUGIN = vpp_plugin.so
CLI_PLUGIN = vpp_cli_plugin.so

# CLI source
CLI_SRCS = src/vpp_cli_plugin.c
CLI_OBJS = $(CLI_SRCS:.c=.o)

# Install directories
PREFIX ?= /usr/local
CLIXON_PLUGIN_DIR ?= $(PREFIX)/lib/clixon/plugins/backend
CLIXON_CLI_DIR ?= $(PREFIX)/lib/clixon/plugins/cli
YANG_DIR ?= $(PREFIX)/share/clixon
CONFIG_DIR ?= /etc/clixon
CLISPEC_DIR ?= $(PREFIX)/share/clixon

# Targets
.PHONY: all clean install uninstall yang check-deps cli

all: check-deps $(PLUGIN)

cli: $(CLI_PLUGIN)

$(PLUGIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ALL_LIBS)
	@echo "Built $(PLUGIN)"

$(CLI_PLUGIN): $(CLI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ALL_LIBS)
	@echo "Built $(CLI_PLUGIN)"

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Dependency header generation
src/vpp_plugin.o: src/vpp_connection.h src/vpp_interface.h
src/vpp_connection.o: src/vpp_connection.h
src/vpp_interface.o: src/vpp_interface.h src/vpp_connection.h

check-deps:
	@echo "Checking dependencies..."
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || { echo "Error: pkg-config not found"; exit 1; }
	@test -f $(VPP_INCLUDE)/vapi/vapi.h || { echo "Error: VPP VAPI headers not found at $(VAPI_INCLUDE)"; echo "Install vpp-dev package or set VPP_PREFIX"; exit 1; }
	@echo "Dependencies OK"

clean:
	rm -f $(OBJS) $(CLI_OBJS) $(PLUGIN) $(CLI_PLUGIN)
	rm -f src/*.o

install: $(PLUGIN)
	@echo "Installing Clixon VPP plugin..."
	install -d $(DESTDIR)$(CLIXON_PLUGIN_DIR)
	install -m 755 $(PLUGIN) $(DESTDIR)$(CLIXON_PLUGIN_DIR)/
	install -d $(DESTDIR)$(YANG_DIR)
	install -m 644 yang/*.yang $(DESTDIR)$(YANG_DIR)/
	install -d $(DESTDIR)$(CONFIG_DIR)
	install -m 644 config/clixon-vpp.xml $(DESTDIR)$(CONFIG_DIR)/
	@if [ -f $(CLI_PLUGIN) ]; then \
		install -d $(DESTDIR)$(CLIXON_CLI_DIR); \
		install -m 755 $(CLI_PLUGIN) $(DESTDIR)$(CLIXON_CLI_DIR)/; \
		echo "  CLI Plugin: $(DESTDIR)$(CLIXON_CLI_DIR)/$(CLI_PLUGIN)"; \
	fi
	@if [ -f cli/vpp.cli ]; then \
		install -m 644 cli/vpp.cli $(DESTDIR)$(CLISPEC_DIR)/; \
		echo "  CLI Spec: $(DESTDIR)$(CLISPEC_DIR)/vpp.cli"; \
	fi
	@echo "Installation complete"
	@echo "  Plugin: $(DESTDIR)$(CLIXON_PLUGIN_DIR)/$(PLUGIN)"
	@echo "  YANG:   $(DESTDIR)$(YANG_DIR)/"
	@echo "  Config: $(DESTDIR)$(CONFIG_DIR)/clixon-vpp.xml"

uninstall:
	rm -f $(DESTDIR)$(CLIXON_PLUGIN_DIR)/$(PLUGIN)
	rm -f $(DESTDIR)$(CLIXON_CLI_DIR)/$(CLI_PLUGIN)
	rm -f $(DESTDIR)$(YANG_DIR)/vpp-*.yang
	rm -f $(DESTDIR)$(CLISPEC_DIR)/vpp.cli
	rm -f $(DESTDIR)$(CONFIG_DIR)/clixon-vpp.xml

# Validate YANG models
yang:
	@echo "Validating YANG models..."
	@for f in yang/*.yang; do \
		echo "  Checking $$f..."; \
		yanglint $$f || exit 1; \
	done
	@echo "YANG validation passed"

# Development targets
.PHONY: dev run-test

dev: CFLAGS += -DDEBUG -O0 -ggdb3
dev: clean all

# Help
help:
	@echo "Clixon VPP Control Plane - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the plugin (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install plugin, YANG models, and config"
	@echo "  uninstall  - Remove installed files"
	@echo "  yang       - Validate YANG models with yanglint"
	@echo "  dev        - Build with debug flags"
	@echo "  help       - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX        - Install prefix (default: /usr/local)"
	@echo "  VPP_PREFIX    - VPP installation prefix (default: /usr)"
	@echo "  DESTDIR       - Destination directory for staged installs"
