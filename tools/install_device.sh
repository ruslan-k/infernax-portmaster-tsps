#!/usr/bin/env bash
# Install the built runtime to the device and refresh the launcher.
# Usage: tools/install_device.sh [build/out] [device]
set -euo pipefail
out=$(realpath "${1:-build/out}")
device=${2:-spruce@192.168.50.135}
port=/mnt/SDCARD/Roms/PORTS/infernax
recipe=$(cd "$(dirname "$0")/.." && pwd)
ssh "$device" "mkdir -p $port /mnt/SDCARD/Roms/ports"
scp -q "$out/infernax-nextos" "$out/run_identity.py" build/image/*.seg build/image/infernax-romfs.section "$device:$port/"
scp -q "$recipe/port/Infernax.sh" "$device:/mnt/SDCARD/Roms/ports/Infernax.sh"
ssh "$device" "chmod +x $port/infernax-nextos /mnt/SDCARD/Roms/ports/Infernax.sh; cd $port && sha256sum infernax-nextos"
