#!/bin/sh
# Builds and runs the host tests for panel_*.h. Needs g++ and the ArduinoJson headers (v7):
#   ARDUINOJSON_DIR=/path/to/ArduinoJson/src ./test/run.sh
# ESPHome keeps a copy under .esphome/ or ~/.platformio/packages after any build; the script looks there too.
set -e
here=$(cd "$(dirname "$0")" && pwd)
aj=${ARDUINOJSON_DIR:-}
if [ -z "$aj" ]; then
  aj=$(find "$here/.." ~/.platformio ~/.esphome -name ArduinoJson.h -path '*src*' 2>/dev/null | head -1 | xargs -r dirname)
fi
[ -n "$aj" ] || { echo "ArduinoJson.h not found; set ARDUINOJSON_DIR"; exit 2; }
g++ -std=c++17 -Wall -Wextra -Wno-format-truncation -O1 -I"$aj" -I"$here/.." -o "$here/panel_test.bin" "$here/panel_test.cpp"
"$here/panel_test.bin"
