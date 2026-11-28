CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lX11 -lm
PKG_CONFIG ?= pkg-config
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags 'gtk4 >= 4.12' 2>/dev/null)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs 'gtk4 >= 4.12' 2>/dev/null)
XFT_CFLAGS = $(shell pkg-config --cflags xft)
XFT_LIBS = $(shell pkg-config --libs xft)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
DESTDIR ?=

.PHONY: all clean install check-gtk check-applications

all: simple-desktop fieldm-packages fieldm-keys fieldm-kbd fieldm-lock fieldm-clock

GTK3_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0 webkit2gtk-4.1 2>/dev/null)
GTK3_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0 webkit2gtk-4.1 2>/dev/null)

DESKTOP_SOURCES = src/desktop.c src/applications.c src/theme.c \
	src/appearance.c src/screen_layout.c src/shell/session.c \
	src/shell/gtk_shell.c src/shell/desktop_view.c src/shell/launcher.c \
	src/shell/settings.c src/shell/wifi.c src/shell/icon_themes.c \
	src/shell/icon_badge.c
DESKTOP_HEADERS = src/applications.h src/appearance.h src/theme.h \
	src/screen_layout.h src/shell/session.h src/shell/gtk_shell.h \
	src/shell/gtk_shell_internal.h src/shell/desktop_view.h \
	src/shell/launcher.h src/shell/settings.h src/shell/icon_themes.h \
	src/shell/icon_badge.h src/shell/wifi.h

check-gtk:
	@command -v "$(PKG_CONFIG)" >/dev/null 2>&1 || { \
		echo "Error: pkg-config is required to build simple-desktop." >&2; \
		exit 1; \
	}
	@$(PKG_CONFIG) --exists 'gtk4 >= 4.12' || { \
		echo "Error: GTK 4.12+ development files and compatible GLib/GIO headers are required." >&2; \
		$(PKG_CONFIG) --cflags 'gtk4 >= 4.12' >&2; \
		exit 1; \
	}

check-applications:
	sh ./tests/check-applications.sh

simple-desktop: $(DESKTOP_SOURCES) $(DESKTOP_HEADERS) | check-gtk
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -std=c11 -o $@ $(DESKTOP_SOURCES) \
		$(LDLIBS) $(GTK_LIBS)

fieldm-packages: src/packages.c src/theme.c src/theme.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -std=c11 -o $@ src/packages.c src/theme.c \
		$(LDLIBS) $(GTK_LIBS)

fieldm-keys: src/fieldm-keys.c
	$(CC) $(CFLAGS) -std=c11 -o $@ src/fieldm-keys.c $(LDLIBS) -lXtst

fieldm-kbd: src/fieldm-kbd.c src/theme.c src/theme.h
	$(CC) $(CFLAGS) $(XFT_CFLAGS) -std=c11 -o $@ src/fieldm-kbd.c src/theme.c \
		$(LDLIBS) $(XFT_LIBS) -lXtst

fieldm-lock: src/fieldm-lock.c
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags gtk+-3.0) -std=c11 -o $@ src/fieldm-lock.c \
		$(LDLIBS) $(shell $(PKG_CONFIG) --libs gtk+-3.0)

fieldm-clock: src/clock.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -std=c11 -o $@ src/clock.c \
		$(LDLIBS) $(GTK_LIBS)

fieldm-browser: src/fieldm-browser.c
	$(CC) $(CFLAGS) $(GTK3_CFLAGS) -std=c11 -o $@ src/fieldm-browser.c \
		$(LDLIBS) $(GTK3_LIBS)

install: all fieldm-browser
	install -Dm755 simple-desktop "$(DESTDIR)$(BINDIR)/simple-desktop"
	install -Dm755 fieldm-packages "$(DESTDIR)$(BINDIR)/fieldm-packages"
	install -Dm755 fieldm-keys "$(DESTDIR)$(BINDIR)/fieldm-keys"
	install -Dm755 fieldm-kbd "$(DESTDIR)$(BINDIR)/fieldm-kbd"
	install -Dm755 fieldm-browser "$(DESTDIR)$(BINDIR)/fieldm-browser"
	install -Dm755 fieldm-lock "$(DESTDIR)$(BINDIR)/fieldm-lock"
	install -Dm755 fieldm-clock "$(DESTDIR)$(BINDIR)/fieldm-clock"
	install -Dm644 data/fieldm.css "$(DESTDIR)/usr/share/fieldm/fieldm.css"
	install -Dm755 scripts/fieldm-hwprobe \
		"$(DESTDIR)/usr/lib/fieldm/hwprobe"
	install -Dm755 scripts/zirarch-post \
		"$(DESTDIR)/usr/bin/zirarch-post"
	install -Dm644 data/field-desktop.desktop \
		"$(DESTDIR)$(XSESSIONDIR)/field-desktop.desktop"
	install -Dm644 data/packages.desktop \
		"$(DESTDIR)/usr/share/applications/packages.desktop"

clean:
	rm -f simple-desktop fieldm-packages fieldm-keys fieldm-kbd fieldm-browser fieldm-lock fieldm-clock