#!/bin/sh
# Removes everything install.sh set up
set -u

LABEL=local.hilink-switch

launchctl bootout "gui/$(id -u)/$LABEL" 2> /dev/null || true
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
rm -rf "$HOME/Library/Application Support/HiLinkSwitch"
rm -f "$HOME/Library/Logs/hilink-switch.log"

echo "hilink-switch uninstalled."
