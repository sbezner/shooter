#!/usr/bin/env bash
# Build the game, record a scripted 30s auto-aim demo (frames + synced audio),
# and encode a textable MP4 (H.264 + AAC, 720p, faststart) with ffmpeg.
#
#   ./make-video.sh [output.mp4]
#
# Needs: an awake display (offscreen GL rendering fails on a sleeping screen --
# we use `caffeinate` to wake/hold it), raylib + ffmpeg (brew install raylib
# ffmpeg). Run from the project folder.
set -euo pipefail
cd "$(dirname "$0")"
eval "$(/opt/homebrew/bin/brew shellenv)" 2>/dev/null || true

OUT="${1:-shooter-demo.mp4}"
WORK="$(mktemp -d)"
FRAMES="$WORK/frames"
mkdir -p "$FRAMES"
trap 'rm -rf "$WORK"' EXIT

echo "==> Building..."
make >/dev/null

echo "==> Recording demo (opens a window briefly, renders ~900 frames)..."
# Wake the display and keep it awake for the run; a sleeping screen makes
# window creation crash.
caffeinate -u -t 3 || true
caffeinate -dimu ./game --record "$FRAMES"

FPS="$(grep -E '#define[[:space:]]+DEMO_FPS' game.c | awk '{print $3}')"
COUNT="$(ls "$FRAMES"/frame_*.png 2>/dev/null | wc -l | tr -d ' ')"
echo "==> Captured ${COUNT} frames at ${FPS} fps; encoding ${OUT} ..."

ffmpeg -y -loglevel error \
  -framerate "$FPS" -i "$FRAMES/frame_%05d.png" \
  -i "$FRAMES/audio.wav" \
  -vf "scale=1280:720:flags=lanczos" \
  -c:v libx264 -pix_fmt yuv420p -crf 22 -preset medium \
  -c:a aac -b:a 128k \
  -movflags +faststart -shortest \
  "$OUT"

echo "==> Done: ${OUT} ($(du -h "$OUT" | awk '{print $1}'))"
