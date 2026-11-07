#!/usr/bin/env bash
# Terminator 2D: NO FATE host build (copy of momodorafarewell-switch-study/native/build.sh @ 08bfe36,
# retargeted: four modules rtld/main/subsdk0/sdk, base RomFS image, optional shader catalogue).
set -euo pipefail
: "${COTM_CC:?Set current NextOS AArch64 compiler}"
: "${COTM_NXAUDIO_DIR:?Set pinned nxaudio provider directory}"
: "${COTM_NXINPUT_DIR:?Set pinned Framework V6 nxinput source directory}"
COTM_NXCOMPAT_DIR=${COTM_NXCOMPAT_DIR:-${COTM_NXINPUT_DIR%/nxinput}/nxcompat}
COTM_NXGL_DIR=${COTM_NXGL_DIR:-${COTM_NXINPUT_DIR%/nxinput}/nxgl}
COTM_NXLOADER_DIR=${COTM_NXLOADER_DIR:-${COTM_NXINPUT_DIR%/nxinput}/nxloader}
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# COTM_SHADERS_DIR: directory holding nvn_shader_sources.h (Frente B catalogue). Unset = the empty stub.
shaders="${COTM_SHADERS_DIR:-$here/shaders_stub}"
if [[ ! -f "$shaders/nvn_shader_sources.h" ]]; then
    echo "shader catalogue not found: $shaders/nvn_shader_sources.h" >&2
    exit 2
fi
if [[ $# != 2 ]]; then
    echo 'Usage: native/build.sh <verified-image-directory> <new-output-directory>' >&2
    exit 2
fi
mkdir "$2"
nxinput_obj_dir="$(mktemp -d "${TMPDIR:-/tmp}/terminator2d-nxinput.XXXXXX")"
trap 'rm -rf -- "$nxinput_obj_dir"' EXIT INT TERM
for source in nxinput_gptk nxinput_gptk_live nxinput_gptk_loader \
    nxinput_gptk_preinit nxinput_gptk4 nxinput_gptk4_bridge \
    nxinput_gptk4_preinit nxinput_gptk_motion nxinput_sha256 \
    nxinput_sdl_seam nxinput_sdl nxinput_portmaster nxinput_sovereign \
    nxinput_authority nxinput_livedb nxinput_decision nxinput_provider \
    nxinput_provider_linux nxinput_translate nxinput_godot nxinput_authority_v5; do
    "$COTM_CC" -O2 -fPIC -pthread -std=c11 -D_POSIX_C_SOURCE=200809L \
        -I"$COTM_NXINPUT_DIR/include" \
        -c "$COTM_NXINPUT_DIR/src/$source.c" \
        -o "$nxinput_obj_dir/$source.o"
done
# Canonical measured provider negotiation and the shared receipt publisher.
# Unrelated NXLoader registry functions are discarded; no private provider copy.
for source in src/nxgl_provider_recovery src/nxgl_arbiter src/nxgl_logic \
    src/nxgl_sdl2 src/nxgl_diagnostics src/nxgl_metrics src/nxgl_present \
    src/nxgl_sdl_hint adapters/nxgl_provider_discovery_adapter \
    adapters/nxgl_nxloader_provider; do
    "$COTM_CC" -O2 -fPIC -pthread -std=gnu99 -ffunction-sections -fdata-sections \
        -I"$COTM_NXGL_DIR/include" -I"$COTM_NXGL_DIR/adapters" -I"$COTM_NXGL_DIR/src" \
        -I"$COTM_NXLOADER_DIR/include" \
        -I"${COTM_SDL_INCLUDE_DIR:-$("$COTM_CC" -print-sysroot)/usr/include/SDL2}" \
        -c "$COTM_NXGL_DIR/$source.c" -o "$nxinput_obj_dir/${source##*/}.o"
done
"$COTM_CC" -O2 -fPIC -pthread -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$COTM_NXINPUT_DIR/include" -I"$COTM_NXINPUT_DIR/engine-glue" \
    -I"${COTM_SDL_INCLUDE_DIR:-$("$COTM_CC" -print-sysroot)/usr/include/SDL2}" \
    -c "$COTM_NXINPUT_DIR/engine-glue/nxc6_glue.c" -o "$nxinput_obj_dir/nxc6_glue.o"
# MOMO_SHADERS selects the SDK 18.x+/Unity NVN layouts of the lineage (16-byte texture view, 0x880 control
# section, framebuffer cache size); it is not a game switch. Kept on the command line because headers that
# test it are included before the catalogue.
# Input is included before the shader catalogue: select T2's physical Switch layout here,
# independently of the later T2_SHADERS define. Other lineage builds keep their existing map.
"$COTM_CC" -O2 -Wall -Wextra -Werror -Wno-error -DMOMO_SHADERS=1 -DINFERNAX_CBUF_SAMPLERS=1 -DZERO_ZX_GAME=1 -DT2_SWITCH_INPUT=1 \
    -DMESA_EGL_NO_X11_HEADERS -fno-strict-aliasing -pthread -I"$1" \
    -I"$COTM_NXAUDIO_DIR" -I"$COTM_NXINPUT_DIR/include" \
    -I"$COTM_NXINPUT_DIR/engine-glue" -I"$COTM_NXCOMPAT_DIR/include" -I"$shaders" \
    -I"$COTM_NXGL_DIR/include" -I"$COTM_NXGL_DIR/adapters" -I"$COTM_NXLOADER_DIR/include" \
    -I"${COTM_SDL_INCLUDE_DIR:-$("$COTM_CC" -print-sysroot)/usr/include/SDL2}" \
    "$here/loader.c" "$here/entry.S" "$here/nvn_bridge.S" \
    "$here/nxgl_frame_proof_adapter.c" "$here/input_gptk.c" \
    "$here/t2_runtime_health.c" \
    "$here/t2_video.c" "$COTM_NXCOMPAT_DIR/src/nxcompat_settings.c" \
    "$COTM_NXCOMPAT_DIR/src/nxcompat_sdl_optional.c" \
    "$COTM_NXCOMPAT_DIR/src/nxcompat_video.c" \
    "$nxinput_obj_dir"/*.o "$COTM_NXAUDIO_DIR/libnxaudio.a" \
    -Wl,--gc-sections ${COTM_SYSROOT:+-Wl,--sysroot=$COTM_SYSROOT} ${COTM_SYSROOT:+-Wl,-rpath-link,$COTM_SYSROOT/usr/lib64} -lSDL2 -lEGL -lGLESv2 -lm -lz -ldl -o "$2/terminator2d-nextos"
if [[ -n "${COTM_SHADERS_DIR:-}" ]]; then
    python3 "$here/sanitize_shader_diagnostics.py" "$2/terminator2d-nextos" --in-place
fi
sha256sum "$2/terminator2d-nextos"
