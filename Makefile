CSTD = -std=c11
OPTFLAGS ?= -Os -ffunction-sections -fdata-sections
WARN ?= -Wall -Wextra -pedantic
HARDEN ?= -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
CFLAGS ?=
LDFLAGS ?=

TINY = -flto -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
TINY_LINK = -Wl,-dead_strip
else
TINY_LINK = -Wl,--gc-sections -Wl,--build-id=none -s
endif

SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)

VERSION := $(shell git describe --tags --abbrev=0 2>/dev/null)

REFRESH = [ -n "$(DESTDIR)" ] || { \
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database $(SHAREDIR)/applications 2>/dev/null; \
  command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(SHAREDIR)/icons/hicolor 2>/dev/null; \
  true; }

ifeq ($(shell id -u),0)
BINDIR ?= /usr/local/bin
SHAREDIR ?= /usr/local/share
else
BINDIR ?= $(HOME)/.local/bin
SHAREDIR ?= $(HOME)/.local/share
endif

all: oshi
	@echo "  install: make install        (you,     ~/.local/bin)"
	@echo "           sudo make install   (system,  /usr/local/bin)"

.version: FORCE
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@

oshi: $(SRC) $(HDR) .version
	$(CC) $(CSTD) $(OPTFLAGS) $(TINY) $(HARDEN) $(WARN) $(CFLAGS) $(LDFLAGS) $(TINY_LINK) \
	  $(if $(VERSION),-DOSHI_VERSION=\"$(VERSION)\") -o $@ $(SRC)

debug: $(SRC) $(HDR)
	$(CC) $(CSTD) -O0 -g $(WARN) $(CFLAGS) -fsanitize=address,undefined \
	  -fno-omit-frame-pointer $(LDFLAGS) \
	  $(if $(VERSION),-DOSHI_VERSION=\"$(VERSION)\") -o oshi-debug $(SRC)

check: oshi
	./oshi --version

release: oshi

install: oshi assets/jetdafih.oshi.desktop assets/jetdafih.oshi.svg
	mkdir -p $(DESTDIR)$(BINDIR) \
	  $(DESTDIR)$(SHAREDIR)/applications \
	  $(DESTDIR)$(SHAREDIR)/icons/hicolor/scalable/apps
	install -m755 oshi $(DESTDIR)$(BINDIR)/oshi
	install -m644 assets/jetdafih.oshi.desktop $(DESTDIR)$(SHAREDIR)/applications/jetdafih.oshi.desktop
	install -m644 assets/jetdafih.oshi.svg $(DESTDIR)$(SHAREDIR)/icons/hicolor/scalable/apps/jetdafih.oshi.svg
	@$(REFRESH)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/oshi $(DESTDIR)$(BINDIR)/Oshi \
	  $(DESTDIR)$(SHAREDIR)/applications/jetdafih.oshi.desktop \
	  $(DESTDIR)$(SHAREDIR)/applications/Oshi.desktop \
	  $(DESTDIR)$(SHAREDIR)/applications/oshi.desktop \
	  $(DESTDIR)$(SHAREDIR)/icons/hicolor/scalable/apps/jetdafih.oshi.svg \
	  $(DESTDIR)$(SHAREDIR)/icons/hicolor/scalable/apps/Oshi.svg \
	  $(DESTDIR)$(SHAREDIR)/icons/hicolor/scalable/apps/oshi.svg
	@$(REFRESH)

clean:
	rm -f oshi oshi-debug Oshi .version

.PHONY: all release debug check clean install uninstall FORCE

FORCE:
