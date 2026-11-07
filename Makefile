PREFIX ?= /usr
CC ?= gcc
PKGS = wayland-client cairo pangocairo
CFLAGS ?= -O2
override CFLAGS += -Wall -Wextra -Isrc -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
BIN = plasma-radial-keyboard
PROTO = protocol/input-method-unstable-v1.xml

SRCS = src/main.c src/render.c src/input.c src/config.c src/blur.c src/panel.c
OBJS = $(patsubst src/%.c, build/%.o, $(SRCS)) build/protocol.o

all: $(BIN)

build/protocol.h: $(PROTO)
	@mkdir -p build
	wayland-scanner client-header $< $@

build/protocol.c: $(PROTO)
	@mkdir -p build
	wayland-scanner private-code $< $@

# Assembly of the generated protocol
build/protocol.o: build/protocol.c build/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

# Building application modules
build/%.o: src/%.c src/app.h build/protocol.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# Linking the binary
$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

test: tests/test_core.c src/input.c src/app.h build/protocol.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/test_core.c src/input.c -o build/test_core $(LDFLAGS) $(LDLIBS)
	./build/test_core

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 data/radialkb.desktop $(DESTDIR)$(PREFIX)/share/applications/radialkb.desktop
	sed -i "s|^Exec=.*|Exec=$(PREFIX)/bin/$(BIN)|" $(DESTDIR)$(PREFIX)/share/applications/radialkb.desktop

clean:
	rm -rf build $(BIN)

.PHONY: all test install clean
