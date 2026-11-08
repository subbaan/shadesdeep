CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -D_GNU_SOURCE
CFLAGS  += $(shell pkg-config --cflags libpng freetype2 sdl3)
LDLIBS  += $(shell pkg-config --libs libpng freetype2 sdl3) -lm -lz

BUILD := build
BIN   := $(BUILD)/shadesdeep
SRC   := $(wildcard src/*.c)
OBJ   := $(SRC:src/%.c=$(BUILD)/%.o)
HDR   := $(wildcard src/*.h)

.PHONY: all link clean

all: $(BIN) link

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

link: $(BIN)
	ln -sfn $(BIN) shadesdeep

clean:
	rm -f $(OBJ) $(BIN)
