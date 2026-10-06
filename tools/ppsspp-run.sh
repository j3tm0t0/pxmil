#!/bin/sh
# Copy the current build into PPSSPP's memory stick and start it there.
#
#   tools/ppsspp-run.sh            (re)start PPSSPP with ./EBOOT.PBP (or $EBOOT)
#
# Based on px68k's tools/ppsspp-run.sh. The debug-port wait is skipped
# until the PSP debug layer is ported.
set -e
cd "$(dirname "$0")/.."
PPSSPP=/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
DIR="$HOME/.config/ppsspp/PSP/GAME/PXMIL"

mkdir -p "$DIR"
pkill -x PPSSPPSDL 2>/dev/null && sleep 1 || true
cp "${EBOOT:-EBOOT.PBP}" "$DIR/EBOOT.PBP"
# Disk images for boot testing, if present
if [ -d roms ]; then
	mkdir -p "$DIR/disk"
	cp roms/* "$DIR/disk/" 2>/dev/null || true
fi
"$PPSSPP" --windowed --escape-exit "$DIR/EBOOT.PBP" >/dev/null 2>&1 &
echo "ppsspp-run: started ($DIR/EBOOT.PBP)"
