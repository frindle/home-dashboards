#!/bin/sh
# Runs on the sandbox, from preview/. Builds LVGL (cached in ~/panel-previews-run/build) and the harness, renders, composes.
set -e
here=$(pwd)
root=$(cd "$here/../../.." && pwd)
run=$HOME/panel-previews-run
find_one() { find "$HOME" "$root" -path "$1" 2>/dev/null | head -1; }
lv=${LVGL_DIR:-$(find "$HOME" -path '*managed_components/lvgl__lvgl/lvgl.h' 2>/dev/null | head -1 | xargs -r dirname)}
aj=${ARDUINOJSON_DIR:-$(find "$HOME" -path '*managed_components/bblanchon__arduinojson/src/ArduinoJson.h' 2>/dev/null | head -1 | xargs -r dirname)}
[ -n "$lv" ] && [ -n "$aj" ] || { echo "need LVGL_DIR and ARDUINOJSON_DIR"; exit 2; }
py=python3
if ! python3 -c "import PIL" 2>/dev/null; then
  [ -x "$run/venv/bin/python" ] || { python3 -m venv "$run/venv" && "$run/venv/bin/pip" install -q pillow; }
  py=$run/venv/bin/python
fi
b=$run/build; mkdir -p "$b"
inc="-I$here -I$lv -I$aj -I$here/.. -DLV_CONF_INCLUDE_SIMPLE"
# LVGL objects, once
if [ ! -f "$b/liblvgl.a" ]; then
  mkdir -p "$b/obj"
  find "$lv/src" -name '*.c' ! -path '*/drivers/*' ! -path '*/debugging/*' > "$b/srcs.txt"
  cat "$b/srcs.txt" | xargs -P 8 -I{} sh -c 'o='"$b"'/obj/$(echo {} | md5sum | cut -c1-12).o; gcc -O1 -w '"$inc"' -c {} -o $o'
  ar rcs "$b/liblvgl.a" "$b"/obj/*.o
fi
g++ -std=c++17 -O1 -Wall -Wno-unused -Wno-format-truncation $inc -o "$b/preview" preview.cpp "$b/liblvgl.a" -lm
w=$run/work; rm -rf "$w"; mkdir -p "$w/out"
$py shots.py "$w"
"$b/preview" "$w/manifest.tsv" "$w/feeds" "$w/out"
r=$run/result; rm -rf "$r"; mkdir -p "$r"
$py compose.py "$w" "$r"
