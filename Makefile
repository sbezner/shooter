# Makefile for the raylib FPS — macOS (Apple Silicon / arm64)
#
# Usage:
#   make        build the `game` binary
#   make run    build (if needed) then launch the game
#   make clean  remove the built binary
#
# We ask pkg-config where raylib's headers and libraries live, and link the
# macOS system frameworks raylib needs for windowing, input, and audio.

CC      = clang
# -arch arm64 targets Apple Silicon explicitly. -std=c99 matches raylib's style.
CFLAGS  = -std=c99 -Wall -Wextra -O2 -arch arm64 $(shell pkg-config --cflags raylib)

# raylib's link flags from pkg-config, plus the Apple frameworks it depends on.
LDFLAGS = $(shell pkg-config --libs raylib) \
          -framework Cocoa \
          -framework IOKit \
          -framework CoreVideo \
          -framework OpenGL

TARGET  = game
SRC     = game.c

# Default target: build the binary.
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

# Build then run.
run: $(TARGET)
	./$(TARGET)

# Remove build artifacts.
clean:
	rm -f $(TARGET)

.PHONY: run clean
