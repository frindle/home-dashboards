#!/bin/bash
# Installs the panel presence agent for the current user. Safe to run again (it updates in place).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
label=local.panel-presence
bin="$HOME/.local/bin/panel-presence.sh"
confdir="$HOME/.config/panel-presence"
conf="$confdir/config"
plist="$HOME/Library/LaunchAgents/$label.plist"

[ "$(uname)" = "Darwin" ] || { echo "macOS only" >&2; exit 1; }

mkdir -p "$HOME/.local/bin" "$confdir" "$HOME/Library/LaunchAgents"
chmod 700 "$confdir"

old_url=$(sed -n 's/^url=//p' "$conf" 2>/dev/null | head -1 || true)
printf 'Dashboard presence URL (e.g. https://host/api/panel/presence)%s: ' "${old_url:+ [$old_url]}"
read -r url
url=${url:-$old_url}
printf 'Shared token (PANEL_PRESENCE_TOKEN; input hidden, Enter keeps the saved one): '
read -rs token; echo
[ -n "$token" ] || token=$(sed -n 's/^token=//p' "$conf" 2>/dev/null | head -1 || true)
[ -n "$url" ] && [ -n "$token" ] || { echo "URL and token are both required" >&2; exit 1; }
case "$url" in *'"'*|*$'\n'*) echo "bad URL" >&2; exit 1;; esac
case "$token" in *'"'*|*$'\n'*) echo "bad token" >&2; exit 1;; esac

umask 077
printf 'url=%s\ntoken=%s\n' "$url" "$token" > "$conf.tmp"
chmod 600 "$conf.tmp"
mv "$conf.tmp" "$conf"

install -m 755 "$here/panel-presence.sh" "$bin"
sed -e "s|__BIN__|$bin|g" -e "s|__LOG__|$confdir/agent.log|g" "$here/$label.plist" > "$plist"
chmod 644 "$plist"

uid=$(id -u)
launchctl bootout "gui/$uid/$label" 2>/dev/null || true
launchctl bootstrap "gui/$uid" "$plist"
echo "Installed. The panel should dim within about 10 s of locking this Mac. Log: $confdir/agent.log"
