#!/bin/bash
# Tells the dashboard whether this Mac's screen is locked, so the desk panel can dim.
# OPTIONAL FALLBACK: the primary source is Home Assistant (PANEL_PRESENCE_ENTITY); only install this if that is not enough.
# Detection: the "CGSSessionScreenIsLocked" flag in the IORegistry (ioreg ships with macOS, needs no permission).
# Posts when the state changes and every 60 s as a heartbeat. Quiet when the dashboard is unreachable.
# Config (never on the command line): ~/.config/panel-presence/config, mode 600, two lines:
#   url=https://dashboard.example/api/panel/presence
#   token=the-shared-token
set -u
CONF="${PANEL_PRESENCE_CONFIG:-$HOME/.config/panel-presence/config}"
BEAT=60     # seconds between heartbeats
POLL=3      # seconds between lock checks

[ "$(stat -f '%Lp' "$CONF" 2>/dev/null)" = "600" ] || { echo "panel-presence: $CONF missing or not mode 600" >&2; exit 78; }
url=$(sed -n 's/^url=//p' "$CONF" | head -1)
token=$(sed -n 's/^token=//p' "$CONF" | head -1)
[ -n "$url" ] && [ -n "$token" ] || { echo "panel-presence: config needs url= and token=" >&2; exit 78; }

is_locked() {
  # Absent when unlocked, <true/> right after the key when locked.
  /usr/sbin/ioreg -n Root -d1 -a 2>/dev/null | /usr/bin/grep -A1 '<key>CGSSessionScreenIsLocked</key>' | /usr/bin/grep -q '<true/>'
}

post() {  # $1 = true|false. URL and token go to curl on stdin, not argv, so ps never shows them.
  printf 'url = "%s"\nheader = "Authorization: Bearer %s"\nheader = "Content-Type: application/json"\n' "$url" "$token" |
    /usr/bin/curl -sS -o /dev/null --max-time 8 -K - --data "{\"locked\":$1}" 2>/dev/null
}

last=""
sent=0
while :; do
  if is_locked; then now=true; else now=false; fi
  t=$(date +%s)
  if [ "$now" != "$last" ] || [ $((t - sent)) -ge $BEAT ]; then
    post "$now" || true
    last=$now
    sent=$t
  fi
  sleep $POLL
done
