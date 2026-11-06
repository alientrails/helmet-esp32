#!/usr/bin/env bash
#
# Builds the full-flash images: bootloader, partition table, boot_app0 and the
# app in one file, written at offset 0x0 over USB.
#
#   pio run -e cyd -e xiao_s3
#   tools/release/merge-images.sh dist cyd xiao_s3
#
# The flash mode, speed and size are taken with esptool's `keep`, so the
# merged image carries exactly the header PlatformIO already wrote from the
# board definition. Repeating the board's numbers here is how a merged image
# ends up disagreeing with the one `pio run -t upload` would have flashed.
#
# What is env-specific is the chip and where its bootloader lives: the ESP32
# keeps the first 4 KB of flash for itself, the S3 and C3 do not.
#
# Used by .github/workflows/release.yml, and by hand when `bin/` needs
# refreshing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:?usage: merge-images.sh OUTPUT_DIR ENV...}"
shift
[ "$#" -gt 0 ] || { echo "merge-images.sh: no environments given" >&2; exit 2; }

# esptool and boot_app0 both come out of the PlatformIO package tree, which is
# wherever PLATFORMIO_CORE_DIR points (~/.platformio by default).
PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
ESPTOOL="$PIO_HOME/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
for f in "$ESPTOOL" "$BOOT_APP0"; do
  [ -f "$f" ] || { echo "merge-images.sh: missing $f -- run pio run first" >&2; exit 1; }
done

mkdir -p "$OUT"

for env in "$@"; do
  build="$ROOT/.pio/build/$env"
  case "$env" in
    # The ESP32's ROM loads its second-stage bootloader from 0x1000. Every
    # later family loads it from 0x0.
    cyd)      chip=esp32   bootloader_at=0x1000 name=cyd ;;
    xiao_s3)  chip=esp32s3 bootloader_at=0x0    name=xiao-s3 ;;
    xiao_c3)  chip=esp32c3 bootloader_at=0x0    name=xiao-c3 ;;
    *) echo "merge-images.sh: unknown environment '$env'" >&2; exit 2 ;;
  esac

  for f in bootloader.bin partitions.bin firmware.bin; do
    [ -f "$build/$f" ] || {
      echo "merge-images.sh: $env has no $f -- run 'pio run -e $env' first" >&2
      exit 1
    }
  done

  # The app image on its own as well: that, not the merged one, is what the
  # phone sends over BLE. Feeding a merged image to the updater would fail its
  # verify, since it starts with a bootloader rather than an app.
  cp "$build/firmware.bin" "$OUT/firmware-$name.bin"

  python3 "$ESPTOOL" --chip "$chip" merge_bin \
    -o "$OUT/firmware-$name-merged.bin" \
    --flash_mode keep --flash_freq keep --flash_size keep \
    "$bootloader_at" "$build/bootloader.bin" \
    0x8000 "$build/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$build/firmware.bin"

  echo "merged $env -> $OUT/firmware-$name-merged.bin"
done
