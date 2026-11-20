#!/bin/bash
# Removes the panel presence agent, its script and its config. Safe to run twice.
label=local.panel-presence
launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$label.plist" "$HOME/.local/bin/panel-presence.sh"
rm -rf "$HOME/.config/panel-presence"
echo "Removed. The panel returns to its normal brightness within 3 minutes (the last report goes stale)."
