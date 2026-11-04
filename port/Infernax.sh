#!/bin/sh
# Infernax (NextOS/NVN). Runs from its own install directory: the runtime opens
# infernax-romfs.section and the *.seg module files relative to the working directory.
PORTDIR=/mnt/SDCARD/Roms/PORTS/infernax

# Launch trace: the menu path and the SSH path differ, so record who ran us.
{ echo "=== $(date) uid=$(id -u) cwd=$(pwd) argv=$0"; } >> /tmp/infernax-launch.log 2>&1

LOG="$PORTDIR/infernax.log"
[ -w "$PORTDIR" ] || LOG=/tmp/infernax.log
cd "$PORTDIR" || exit 1

# PortMaster/NextOS handoff when the framework is installed; a bare run leaves the
# display and the audio device held by the frontend.
controlfolder=""
for c in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
         /mnt/SDCARD/Persistent/portmaster/PortMaster /roms/ports/PortMaster; do
    [ -f "$c/control.txt" ] && { controlfolder=$c; break; }
done
if [ -n "$controlfolder" ]; then
    . "$controlfolder/control.txt" 2>/dev/null
    [ -f "$controlfolder/funcs.txt" ] && . "$controlfolder/funcs.txt" 2>/dev/null
fi

# cores 0-3 must be online: an affinity mismatch aborts the runtime.
for c in 0 1 2 3; do
    p=/sys/devices/system/cpu/cpu$c/online
    [ -e "$p" ] || continue
    ${ESUDO:-} chmod 644 "$p" 2>/dev/null   # exported 0444; root still cannot write it as-is
    [ "$(cat "$p" 2>/dev/null)" = 0 ] && ${ESUDO:-} sh -c "echo 1 > $p" 2>/dev/null
done

export COTM_GL_THREAD=0 COTM_SINGLE_CONTEXT=0 COTM_ASYNC_FRAME=0
# The automatic GPU pipeline leaves command lists uncompleted: their poll is only installed for the LC2
# async path, so the reuse guard trips at frame 4 (NVN_COMMAND_REUSE_BEFORE_COMPLETION handle=2).
export T2_GPU_PIPELINE=0
# 963 MiB board: the game reaches ~690 MiB RSS while loading 385 MiB of textures, which trips the
# 140 MiB guard before the first scene. Lower it for bring-up; raise it again once the footprint is known.
# Boot load takes >30 s on this board (asset decode); the loader watchdog defaults to 20 s.
export MEGAMAN_PROBE_SECONDS="${MEGAMAN_PROBE_SECONDS:-240}"
# Bring-up: the title stays black while assets decode; keep the black-streak guard from ending the run.
# Bring-up diagnostic: magenta quad into the guest target, to separate guest geometry from present.
# Physical swap-chain check: clear the window to magenta before the swap.
# Catch the first GL error per command record instead of at list submit.
export COTM_GL_ERROR_PER_LIST="${COTM_GL_ERROR_PER_LIST:-1}"
export NX_PRESENT_CLEAR="${NX_PRESENT_CLEAR:-0}"
export NX_PROBE_NO_DEPTH="${NX_PROBE_NO_DEPTH:-0}"
export NX_PROBE_QUAD="${NX_PROBE_QUAD:-0}"
export COTM_BLACK_LIMIT="${COTM_BLACK_LIMIT:-3000}"
export T2_MEMORY_FLOOR_MIB="${T2_MEMORY_FLOOR_MIB:-32}"
export SDL_VIDEODRIVER=kmsdrm SDL_AUDIODRIVER=alsa
# The firmware's bundled libasound (LD_LIBRARY_PATH=/usr/trimui/lib) cannot resolve
# the "default" PCM here; name the hardware device explicitly.
export AUDIODEV=plughw:0,0
[ -n "${sdl_controllerconfig:-}" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# NX_SHADER_DUMP=1 writes nvn-program-{code,control}-<run>-0.bin for the first
# program with no catalogue entry (needs a numeric COTM_RUN_ID).
export NX_SHADER_DUMP="${NX_SHADER_DUMP:-1}"
# A fixed default run id makes the second launch fail on its own receipt directory
# (EACCES/EEXIST); the runtime needs 1-12 digits.
export COTM_RUN_ID="${COTM_RUN_ID:-$(date +%s)}"
export MOMO_TRACE_UNIFORM_BINDS="${MOMO_TRACE_UNIFORM_BINDS:-1}"
echo "COTM_RUN_ID=$COTM_RUN_ID NX_CBUF_TEXTURE=${NX_CBUF_TEXTURE:-1}" >> /tmp/infernax-launch.log 2>&1

if command -v pm_platform_helper >/dev/null 2>&1; then
    pm_platform_helper "$PORTDIR/infernax-nextos"
fi

: > "$LOG"
echo "COTM_RUN_ID=$COTM_RUN_ID NX_CBUF_TEXTURE=${NX_CBUF_TEXTURE:-1}" >>"$LOG"
./infernax-nextos >>"$LOG" 2>&1
status=$?
echo "exit=$status" >> "$LOG"
{ echo "--- exit=$status $(date)"; } >> /tmp/infernax-launch.log 2>&1
command -v pm_finish >/dev/null 2>&1 && pm_finish
exit $status
