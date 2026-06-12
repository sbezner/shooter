# HANDOFF — fps-game (raylib native FPS)

A resume guide for this project. Read this first when picking the work back up.
Last updated: 2026-06-12.

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
| R | Reload (restart when the round is over) |
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

## Dev tip: demo video
`./make-video.sh [out.mp4]` builds, runs `./game --record <dir>` (a scripted
auto-aim demo — drives the camera, locks onto beers, fires, ~30 s), and encodes
a textable H.264/AAC 720p MP4 with ffmpeg. The demo writes one PNG per frame
plus a synthesised `audio.wav` (the shoot/hit tones are re-mixed into a track via
`MixTone()` at each shot's timestamp, so picture and sound stay in sync). Tunables:
`DEMO_FPS`, `DEMO_SECONDS` in `game.c`. Needs an awake display and `ffmpeg`
(`brew install ffmpeg`). `*.mp4` is gitignored.

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
1. **Score-attack mode + bonus beers** — each round is a 60s countdown
   (`ROUND_SECONDS`); time's up freezes play behind a "TIME'S UP" overlay with
   the round score, session best, and "press R to restart". ~22% of respawns
   (`BONUS_CHANCE`) come back as **golden bonus beers**: smaller (`BONUS_SIZE`),
   faster, gold-tinted with a RED beacon, worth `BONUS_POINTS` (3). The hit
   marker now shows the points earned (+1/+3). The countdown is skipped in
   `--shot`/`--record` modes so the dev tools behave as before.
2. **Shotgun + beer graphics** — enemies are now camera-facing **beer-mug
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
  `ENEMY_COUNT`, `ENEMY_SIZE`, game-feel (`FIRE_COOLDOWN`, `SHELLS_MAX`,
  `RELOAD_TIME`), score attack (`ROUND_SECONDS`, `BONUS_CHANCE`, `BONUS_POINTS`,
  `BONUS_SIZE`), plus the shotgun viewmodel block `GUN_W`, `GUN_ROT`,
  `GUN_ANCHOR_X/Y`, `GUN_ORIGIN_FX/FY`, `GUN_MUZZLE_FX/FY`.
- `Enemy` struct: `position`, `velocity` (XZ drift), `size`, `bonus`, `alive`.
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
- Enemies that chase the player or shoot back; player health.
- Persist the best score across runs (write/read a small high-score file).
- Combo multiplier for consecutive hits without a miss.
- Gun tuning: flash intensity (position/size/angle are already tunable via the
  `GUN_*` defines).

## Notes / gotchas
- raylib uses `GetScreenToWorldRay` (raylib 5.x); older code/tutorials call it
  `GetMouseRay`.
- GitHub on this account uses **email 2FA** to sbezner@gmail.com (not the phone app);
  verification codes arrive by email (search Gmail `from:github.com`).
- The compiled `game` binary is intentionally NOT in git — rebuild with `make`.
