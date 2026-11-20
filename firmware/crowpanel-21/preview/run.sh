#!/bin/sh
# One command: renders every screen on the sandbox (Linux) and copies the PNGs back.
#   firmware/crowpanel-21/preview/run.sh [DEST_DIR]        (default DEST_DIR: ~/panel-previews)
# Nothing is built on the Mac. Needs ssh access to `claude-sandbox`, and on it: g++, python3 with Pillow
# (pip install --user pillow), the LVGL 9.5 sources and the ArduinoJson 7 headers (an ESPHome build leaves both under
# <firmware>/.esphome/build/*/managed_components; or set LVGL_DIR / ARDUINOJSON_DIR on the sandbox).
set -e
here=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$here/.." && pwd)
dest=${1:-$HOME/panel-previews}
host=${SANDBOX:-claude-sandbox}
rd=panel-previews-run
ssh "$host" "mkdir -p $rd/src"
rsync -a --delete --exclude .git --exclude node_modules --exclude .esphome --exclude '*.bin' "$fw/" "$host:$rd/src/crowpanel-21/"
ssh "$host" "cd $rd/src/crowpanel-21/preview && sh remote.sh"
mkdir -p "$dest"
rsync -a --delete "$host:$rd/result/" "$dest/"
echo "previews in $dest"
