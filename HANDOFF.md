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
- `assets/` — `beer.png` (enemy sprite, OpenMoji 🍺, CC BY-SA 4.0) and
  `shotgun.png` (viewmodel, OpenGameArt "2D Guns", CC0). Loaded with relative
  paths, so **run the game from the project folder** (`make run` does this).
- `README.md` — public-facing description.
- `.gitignore` — ignores the compiled `game` binary, `.DS_Store`, and dev
  screenshots (`preview.png`, `crop.png`).
- `HANDOFF.md` — this file.

## Dev tip: offscreen screenshots
`./game --shot out.png` renders a few frames (a beer parked in front, gun
firing), writes `out.png`, and exits — without grabbing the mouse. Handy for
eyeballing the viewmodel. Needs an awake display; if the screen is asleep/locked,
window creation fails (GLFW "Failed to determine Monitor"). The shotgun's on-
screen placement is tuned via the `GUN_*` `#define`s at the top of `game.c`; the
muzzle-flash position is derived from those with the same transform raylib uses
to draw the sprite, so moving the gun moves the flash automatically.

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
1. **Shotgun + beer graphics** — enemies are now camera-facing **beer-mug
   billboards** (`DrawBillboard`) and the viewmodel is a **shotgun sprite**
   (`DrawTexturePro`, flipped, pivoted/rotated via the `GUN_*` defines). Added
   `assets/` (downloaded art), a `--shot` offscreen-screenshot dev mode, and
   texture load/unload. Replaced the old red cubes and the procedural blaster.
2. **Gun viewmodel + muzzle flash + recoil** (commit `496607b`) — 2D blaster in the
   lower-right that flashes and kicks when firing.
2. **Sound effects + patrolling enemies** (commit `c99acb0`) — procedurally generated
   shoot/hit tones via `GenTone()`; enemies move and bounce off the arena bounds.
3. **Initial game** (commit `4a2d6d0`) — first-person camera, arena with walls,
   crosshair, ray-cast shooting at cubes, score + FPS HUD, respawn on hit. Later
   tweaks added enemy beacons, shot tracers, hit markers, and a bigger crosshair.

## Code map (game.c)
- Tunable `#define`s up top: `SCREEN_WIDTH/HEIGHT`, `ARENA_HALF`, `WALL_*`,
  `ENEMY_COUNT`, `ENEMY_SIZE`, plus the shotgun viewmodel block `GUN_W`,
  `GUN_ROT`, `GUN_ANCHOR_X/Y`, `GUN_ORIGIN_FX/FY`, `GUN_MUZZLE_FX/FY`.
- `Enemy` struct: `position`, `velocity` (XZ drift), `alive`.
- `RandFloat()` — random float helper.
- `RespawnEnemy()` — random position + random heading/speed.
- `GenTone(startFreq, endFreq, seconds)` — builds a `Sound` from a generated sine
  sweep with a fade-out; no audio files needed.
- `main()`:
  - Setup: parse `--shot`, window, audio device, **load `beerTex`/`shotgunTex`**,
    camera (CAMERA_FIRST_PERSON), `DisableCursor()`, generate sounds, spawn enemies.
  - UPDATE: `UpdateCamera`, move/bounce enemies, handle fire (raycast via
    `GetScreenToWorldRay` + `GetRayCollisionBox`, closest hit wins), set timers,
    play sounds.
  - DRAW: 3D (floor, walls, **beer billboards** + beacons, tracer, grid) →
    **shotgun viewmodel** (`DrawTexturePro`, flipped) + muzzle flash → HUD
    (crosshair/hit-marker, score, FPS, controls).
  - Cleanup: **unload textures**, unload sounds, close audio, `EnableCursor()`,
    close window.

## Ideas not yet built
- **Score-attack mode**: 60s countdown + "Time's up! Score: N — press R to restart"
  game-over screen (teaches game states + restart logic).
- Enemies that chase the player or shoot back; player health.
- Different enemy types worth different points.
- Gun tuning: flash intensity, fire-rate limit for hold-to-fire, a subtle
  idle sway/bob (position/size/angle are already tunable via the `GUN_*` defines).

## Notes / gotchas
- raylib uses `GetScreenToWorldRay` (raylib 5.x); older code/tutorials call it
  `GetMouseRay`.
- GitHub on this account uses **email 2FA** to sbezner@gmail.com (not the phone app);
  verification codes arrive by email (search Gmail `from:github.com`).
- The compiled `game` binary is intentionally NOT in git — rebuild with `make`.
