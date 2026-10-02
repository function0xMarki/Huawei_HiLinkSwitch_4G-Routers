#!/bin/sh
# Installs hilink-switch for the current user: builds the tool and sets up a
# LaunchAgent that runs it every time the router is plugged in.
# Needs no sudo and touches nothing outside ~/Library.
set -eu
umask 077
cd "$(dirname "$0")"

LABEL=local.hilink-switch
DEST="$HOME/Library/Application Support/HiLinkSwitch"
BIN="$DEST/hilink-switch"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/hilink-switch.log"
DOMAIN="gui/$(id -u)"

# The program uses APIs from macOS 12
major=$(sw_vers -productVersion | cut -d. -f1)
if [ "$major" -lt 12 ]; then
    echo "HiLinkSwitch needs macOS 12 or later (this is $(sw_vers -productVersion))." >&2
    exit 1
fi
if ! xcode-select -p > /dev/null 2>&1; then
    echo "Building needs the Xcode Command Line Tools. Install them with:" >&2
    echo "  xcode-select --install" >&2
    echo "and run this script again." >&2
    exit 1
fi

echo "Building..."
mkdir -p build
clang -O2 -Wall -arch x86_64 -arch arm64 -mmacosx-version-min=12.0 \
    -o build/hilink-switch hilink-switch.c \
    -framework IOKit -framework CoreFoundation -framework DiskArbitration

echo "Installing to $DEST"
mkdir -p "$DEST" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
chmod 700 "$DEST"
install -m 700 build/hilink-switch "$BIN"
touch "$LOG"
chmod 600 "$LOG"

# plutil writes the paths with proper XML escaping, whatever $HOME holds
cp local.hilink-switch.plist.in "$PLIST"
chmod 600 "$PLIST"
plutil -insert ProgramArguments.0 -string "$BIN" "$PLIST"
plutil -insert StandardErrorPath -string "$LOG" "$PLIST"
plutil -lint "$PLIST" > /dev/null

# Right after a bootout, bootstrap can fail for a moment with error 5
launchctl bootout "$DOMAIN/$LABEL" 2> /dev/null || true
tries=0
until launchctl bootstrap "$DOMAIN" "$PLIST" 2> /dev/null; do
    tries=$((tries + 1))
    if [ "$tries" -ge 5 ]; then
        launchctl bootstrap "$DOMAIN" "$PLIST"
        exit 1
    fi
    sleep 1
done

echo "Done. The router will switch to network mode on its own when plugged in."
echo "Log: $LOG"
