#!/bin/sh
# make_icns.sh <makeicon> <logo.png> <out.icns>
# Builds the app icon in every size macOS needs (16 px to 1024 px) from one square logo.
set -e
MAKEICON="$1"
LOGO="$2"
OUT="$3"
WORK="$(dirname "$OUT")/Folix.iconset"

rm -rf "$WORK"
mkdir -p "$WORK"
"$MAKEICON" "$LOGO" "$WORK/master.png"
for s in 16 32 128 256 512; do
  sips -z $s $s "$WORK/master.png" --out "$WORK/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d "$WORK/master.png" --out "$WORK/icon_${s}x${s}@2x.png" >/dev/null
done
cp "$WORK/master.png" "$(dirname "$OUT")/Folix-1024.png"
rm "$WORK/master.png"
iconutil -c icns "$WORK" -o "$OUT"
