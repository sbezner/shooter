# HANDOFF — fps-game (raylib native FPS)

A resume guide for this project. Read this first when picking the work back up.
Last updated: 2026-06-01.

## What this is
A tiny **native** first-person shooter for macOS (Apple Silicon / arm64), written
in C with [raylib](https://www.raylib.com/). It compiles to a real Mach-O arm64
binary — no browser, Electron, WebGL, or game engine.

- **GitHub:** https://github.com/sbezner/shooter (public)
- **Local path:** `~/projects/shooter/fps-game`
- **Owner GitHub user:** `sbezner`

## How to build & run
From the project folder:
```bash
cd ~/projects/shooter/fps-game
make          # build the `game` binary
make run      # build + launch
make clean    # delete the binary
```
On first launch macOS may ask to allow input monitoring / "Open Anyway"
(System Settings → Privacy & Security) because the game captures the mouse.

## Controls
| Input | Action |
|-------|--------|
| Mouse | Look |
| W A S D | Move |
| Left-click / Space | Shoot |
| ESC | Quit (unlocks the cursor) |

## Files
- `game.c` — the whole game, one file, heavily commented.
- `Makefile` — clang build, `-arch arm64`, uses `pkg-config --cflags --libs raylib`
  plus the Cocoa/IOKit/CoreVideo/OpenGL frameworks.
- `README.md` — public-facing description.
- `.gitignore` — ignores the compiled `game` binary and `.DS_Store`.
- `HANDOFF.md` — this file.

## Environment (already set up on this Mac — don't reinstall)
- **Xcode Command Line Tools** (clang 21, make, git). Was installed *headlessly* via
  `softwareupdate` (no GUI click), not the popup.
- **Homebrew** at `/opt/homebrew` — added to PATH in `~/.zprofile`.
- **raylib 5.5** and **pkg-config** via `brew install raylib pkg-config`.
- **GitHub CLI** (`gh`), logged in as `sbezner`, with `gh auth setup-git` done so
  `git push` works over HTTPS without prompting.
- If `brew`/`gh` aren't on PATH in a fresh shell: `eval "$(/opt/homebrew/bin/brew shellenv)"`.

## Saving changes (git workflow)
```bash
git add -A
git commit -m "describe the change"
git push
```

## Feature history (newest first)
1. **Gun viewmodel + muzzle flash + recoil** (commit `496607b`) — 2D blaster in the
   lower-right that flashes and kicks when firing.
2. **Sound effects + patrolling enemies** (commit `c99acb0`) — procedurally generated
   shoot/hit tones via `GenTone()`; enemies move and bounce off the arena bounds.
3. **Initial game** (commit `4a2d6d0`) — first-person camera, arena with walls,
   crosshair, ray-cast shooting at cubes, score + FPS HUD, respawn on hit. Later
   tweaks added enemy beacons, shot tracers, hit markers, and a bigger crosshair.

## Code map (game.c)
- Tunable `#define`s up top: `SCREEN_WIDTH/HEIGHT`, `ARENA_HALF`, `WALL_*`,
  `ENEMY_COUNT`, `ENEMY_SIZE`.
- `Enemy` struct: `position`, `velocity` (XZ drift), `alive`.
- `RandFloat()` — random float helper.
- `RespawnEnemy()` — random position + random heading/speed.
- `GenTone(startFreq, endFreq, seconds)` — builds a `Sound` from a generated sine
  sweep with a fade-out; no audio files needed.
- `main()`:
  - Setup: window, audio device, camera (CAMERA_FIRST_PERSON), `DisableCursor()`,
    generate `shootSound`/`hitSound`, spawn enemies.
  - UPDATE: `UpdateCamera`, move/bounce enemies, handle fire (raycast via
    `GetScreenToWorldRay` + `GetRayCollisionBox`, closest hit wins), set timers,
    play sounds.
  - DRAW: 3D (floor, walls, enemy cubes + beacons, tracer, grid) → gun viewmodel
    (2D) → HUD (crosshair/hit-marker, score, FPS, controls).
  - Cleanup: unload sounds, close audio, `EnableCursor()`, close window.

## Ideas not yet built
- **Score-attack mode**: 60s countdown + "Time's up! Score: N — press R to restart"
  game-over screen (teaches game states + restart logic).
- Enemies that chase the player or shoot back; player health.
- Different enemy types worth different points.
- Gun tuning: position/size, flash intensity, fire-rate limit for hold-to-fire.

## Notes / gotchas
- raylib uses `GetScreenToWorldRay` (raylib 5.x); older code/tutorials call it
  `GetMouseRay`.
- GitHub on this account uses **email 2FA** to sbezner@gmail.com (not the phone app);
  verification codes arrive by email (search Gmail `from:github.com`).
- The compiled `game` binary is intentionally NOT in git — rebuild with `make`.
