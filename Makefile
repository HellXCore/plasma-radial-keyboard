PREFIX ?= /usr
CC ?= gcc
PKGS = wayland-client cairo pangocairo
CFLAGS ?= -O2
override CFLAGS += -Wall -Wextra -Isrc -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
BIN = plasma-radial-keyboard
PROTO = protocol/input-method-unstable-v1.xml

all: $(BIN)

build/protocol.h: $(PROTO)
	@mkdir -p build
	wayland-scanner client-header $< $@

build/protocol.c: $(PROTO)
	@mkdir -p build
	wayland-scanner private-code $< $@

$(BIN): src/main.c src/kb_core.h build/protocol.h build/protocol.c
	$(CC) $(CFLAGS) src/main.c build/protocol.c -o $@ $(LDFLAGS) $(LDLIBS)

test: tests/test_core.c src/kb_core.h
	@mkdir -p build
	$(CC) -Wall -Wextra -Isrc tests/test_core.c -o build/test_core -lm
	./build/test_core

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 data/radialkb.desktop $(DESTDIR)$(PREFIX)/share/applications/radialkb.desktop
	sed -i "s|^Exec=.*|Exec=$(PREFIX)/bin/$(BIN)|" $(DESTDIR)$(PREFIX)/share/applications/radialkb.desktop

clean:
	rm -rf build $(BIN)

.PHONY: all test install clean
