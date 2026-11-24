#!/usr/bin/env bash
# Regenerates the LVGL fonts in src/fonts/ from the Outfit typeface (SIL OFL 1.1).
# Needs Node.js. Run from the repo root: ./tools/gen_fonts.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cd "$WORK"
npm init -y >/dev/null
npm i --silent lv_font_conv@1.5.3 @fontsource/outfit@5.3.0

F=node_modules/@fontsource/outfit/files/outfit-latin
C=node_modules/.bin/lv_font_conv
O="$ROOT/src/fonts"
COMMON=(--bpp 4 --no-compress --format lvgl --lv-include lvgl.h)

# Big clock digits
$C --font $F-300-normal.woff --size 290 --symbols "0123456789:-" "${COMMON[@]}" --lv-font-name font_clock -o "$O/font_clock.c"
# Inside / outside readings on the main screen
$C --font $F-300-normal.woff --size 56 --symbols "0123456789-.°%" "${COMMON[@]}" --lv-font-name font_value -o "$O/font_value.c"
# Sunrise / sunset times
$C --font $F-400-normal.woff --size 28 --symbols "0123456789:- APM" "${COMMON[@]}" --lv-font-name font_sun -o "$O/font_sun.c"
# The ringing screen (time, SNOOZE)
$C --font $F-300-normal.woff --size 76 --symbols "0123456789-.°%: APMSNOZE" "${COMMON[@]}" --lv-font-name font_reading -o "$O/font_reading.c"
# Medium text (date, AM/PM)
$C --font $F-400-normal.woff --size 34 --range 0x20-0x7E --symbols "°·" "${COMMON[@]}" --lv-font-name font_md -o "$O/font_md.c"
# Small labels
$C --font $F-500-normal.woff --size 22 --range 0x20-0x7E --symbols "°·" "${COMMON[@]}" --lv-font-name font_sm -o "$O/font_sm.c"

echo "Fonts written to $O"
