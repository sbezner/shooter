# shooter

A tiny **native** first-person shooter for macOS (Apple Silicon / arm64), written in C with [raylib](https://www.raylib.com/). No browser, no Electron, no game engine — it compiles to a real Mach-O binary.

![arena](https://img.shields.io/badge/platform-macOS%20arm64-blue)

## Controls

| Input | Action |
|-------|--------|
| Mouse | Look |
| W A S D | Move |
| Left-click / Space | Shoot |
| ESC | Quit |

Shoot the red cubes (marked with yellow beacons). They **patrol around the arena**, so aiming takes some skill. Each hit scores a point, plays a sound, and respawns the cube elsewhere. Firing and hitting both have procedurally-generated **sound effects** (no audio files needed).

## Build & run

Requires the Xcode Command Line Tools, plus raylib and pkg-config via [Homebrew](https://brew.sh):

```bash
brew install raylib pkg-config
make        # builds the `game` binary
make run    # build + launch
```

## Files

- `game.c` — the entire game (~280 lines, heavily commented)
- `Makefile` — clang build targeting arm64, links raylib + macOS frameworks
