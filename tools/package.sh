#!/usr/bin/env bash
# package.sh — Phase 5: codesign + install the built VST3/AU (macOS).
# Run AFTER `cmake --build build`. Ad-hoc sign for local loading; use a Developer
# ID for distribution.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
INSTALL="${INSTALL:-1}"            # set INSTALL=0 to sign-only
DEV_ID="${DEV_ID:-}"               # e.g. "Developer ID Application: You (TEAMID)"; empty=ad-hoc

SIGN_IDENTITY="${DEV_ID:--}"       # "-" = ad-hoc
VST3="$(ls -d "$BUILD_DIR/VB1Reimpl_artefacts/Release/VST3/"*.vst3 2>/dev/null | head -1 || true)"
AU="$(ls -d "$BUILD_DIR/VB1Reimpl_artefacts/Release/AU/"*.component 2>/dev/null | head -1 || true)"

[ -z "$VST3" ] && [ -z "$AU" ] && { echo "No built plugin found under $BUILD_DIR. Run cmake --build first."; exit 1; }

sign() { # $1 = bundle path
  echo "codesign $SIGN_IDENTITY: $1"
  codesign --force --deep --sign "$SIGN_IDENTITY" "$1"
  codesign --verify --verbose=2 "$1"
}

[ -n "$VST3" ] && sign "$VST3"
[ -n "$AU"   ] && sign "$AU"

if [ "$INSTALL" = "1" ]; then
  echo "installing to ~/Library/Audio/Plug-Ins ..."
  mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
  [ -n "$VST3" ] && cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/
  [ -n "$AU"   ] && cp -R "$AU"   ~/Library/Audio/Plug-Ins/Components/
  echo "done. Restart your DAW / run: killall -9 AudioComponentRegistrar 2>/dev/null || true"
fi
