# CrowPanel 2.1" status panel (ESPHome)

Read-only round display for the home lab. It polls the dashboard's `GET /api/panel` every 20 s (one small JSON with
short keys, documented at the top of `lib/panelFeed.ts`). Nothing on the panel writes anywhere.

Board: Elecrow CrowPanel Rotary 2.1" (ESP32-S3R8, 8 MB PSRAM, 16 MB flash, ST7701S 480x480, CST816 touch, encoder
A=GPIO42 B=GPIO4, encoder press on PCF8574 P5). The display, PCF8574 and encoder setup come from Elecrow's ESPHome
lesson 05.

## How to use it
- Home is the Darkbloom overview. Turn the knob to move between topics: Darkbloom, Network, Storage, EV tonight, Clock.
- Press to open a topic's detail pages, turn to move through them, press again to go back.
- After 2 minutes without touching the knob it returns to the Darkbloom overview (the alert screen stays until you act).
- Ring colour: blue ok, amber attention, red act, dark grey no link.

### What each page shows
- Darkbloom overview: earned today (all Macs), serving count, month, watts, silent Mac or open alerts. Detail: one page per
  Mac (up to 4, shown as the feed lists them, no reflash).
- Network overview: total down, and each link's down/up in Mbps. Detail: one page per link with 24 h / 7 day / 30 day
  GB down/up (rolling windows; the label switches to today/week/month if the feed ever sends calendar periods).
- Storage overview: percent used, TB, disks spinning, array and parity state, hottest disk. Detail: an array page, then
  the disks four per page (usage, temperature, spinning).
- EV tonight: state of charge, plug, target and hours, amps, kWh, short-of-target warning. Detail: the plan.
- Clock: time, date and the current reasons if not all ok.
- Alert: shown automatically on red.

### Red auto-switch
When the feed goes red it carries a `go` target and reason keys. The panel jumps to the Alert screen. Press opens the
silent Mac or Storage; turn or press snoozes it for 15 minutes. A new red reason jumps at once, even while snoozed.
When red clears, the panel goes home. Amber never jumps, it only tints the ring.

### Missing data
- Dashboard not reachable: "WAIT" for the first 45 s after boot, then "NO LINK" with the age of the last data. The panel
  hides numbers older than 75 s.
- One section down: "NO DATA" on that page only. A section the dashboard is serving from its last-good copy shows
  "old data" in amber. Network with only Unraid traffic says so in the footer.

## Set up
1. `cp secrets.example.yaml secrets.yaml` and fill in `panel_url` (the dashboard address, port and `/api/panel`),
   `ota_password` and `ap_password` (made-up strings). `secrets.yaml` is gitignored.
2. On the dashboard host set the "Panel feed" variables in the repo `.env.example` (`PANEL_BLOOM_LEDGER_URL`,
   `PANEL_NETMON_URL`, `PANEL_INFLUX_BUCKET`, optional `PANEL_INFLUX_TOKEN`, `PANEL_NETMON_TOTALS_MAP`). Night dimming is
   off until `PANEL_NIGHT_HOURS` is set (with `PANEL_NIGHT_BRIGHTNESS`, `PANEL_DAY_BRIGHTNESS`, `PANEL_TZ`).
3. `pip install esphome`, then `esphome config crowpanel-21.yaml` to validate.

## Flash (first time, USB)
Hold BOOT, plug USB into the USB-5V-IN port, release BOOT, then `esphome run crowpanel-21.yaml` and pick the serial port.
Later updates go over Wi-Fi (OTA, password from `secrets.yaml`).

## Wi-Fi setup
Wi-Fi is not in the firmware. After the first flash, either use the Improv Wi-Fi setup in the ESPHome web installer
over USB, or join the "crowpanel-21 setup" hotspot (password `ap_password`) and use the page that opens. The network is
saved on the device and survives OTA updates. The hotspot comes back if the saved network cannot be reached.

## Tests
`test/run.sh` builds and runs the host tests for the parser, navigation, auto-switch and every screen's text (needs
g++ and the ArduinoJson v7 headers; set `ARDUINOJSON_DIR`). The feed side is tested by `npm test` in the repo.

## Knob and tuning
`knob_counts_per_step` and `knob_direction` are substitutions at the top of the YAML: raise the first if one click skips
a screen, set the second to `-1` if clockwise goes backwards.

## Not verified on real hardware
Only `esphome config`, `esphome compile` and the host tests have been run. Check on the device: text fit on the round
screen and colours, knob direction and clicks per step, the press (PCF8574 P5, active low), the Improv and hotspot Wi-Fi
setup, the backlight dimming, and that the panel recovers after the dashboard restarts.

Not included on purpose: Plex streams, queue depth, weather, Studio temperatures, a Lab section (no existing source).
