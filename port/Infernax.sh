#!/bin/sh
# Infernax (NextOS/NVN). Runs from its own install directory: the runtime opens
# infernax-romfs.section and the *.seg module files relative to the working directory.
PORTDIR=/mnt/SDCARD/Roms/PORTS/infernax
LOG="$PORTDIR/infernax.log"
cd "$PORTDIR" || exit 1
# cores 0-3 must be online: an affinity mismatch aborts the runtime.
for c in 0 1 2 3; do
    p=/sys/devices/system/cpu/cpu$c/online
    [ -e "$p" ] || continue
    chmod 644 "$p" 2>/dev/null   # the kernel exposes it 0444; root still cannot write it as-is
    [ "$(cat "$p" 2>/dev/null)" = 0 ] && echo 1 > "$p" 2>/dev/null
done
export COTM_GL_THREAD=0 COTM_SINGLE_CONTEXT=0 COTM_ASYNC_FRAME=0
export SDL_VIDEODRIVER=kmsdrm SDL_AUDIODRIVER=alsa
# NX_SHADER_DUMP=1 writes nvn-program-{code,control}-<run>-0.bin for the first
# program with no catalogue entry (needs a numeric COTM_RUN_ID).
export NX_SHADER_DUMP="${NX_SHADER_DUMP:-0}" COTM_RUN_ID="${COTM_RUN_ID:-1}"
./infernax-nextos > "$LOG" 2>&1
status=$?
echo "exit=$status" >> "$LOG"
exit $status
