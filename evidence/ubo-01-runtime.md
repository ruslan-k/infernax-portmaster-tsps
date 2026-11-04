# Build 31 runtime result — 2026-11-04

## Deployment and principal lifecycle
- Installed binary: `build/out-all-31/infernax-nextos`, SHA-256 `49460524e6888095c486fdea592bb09c24d52e9b60d56e6ab79f260cc1020ebd`.
- Pre-deployment device backup: `infernax-nextos.bak-0031`, SHA-256 `f648be4d716e0702291d3fad50446fdd4a64f9ff2f603a2fb9da233178ef4760`.
- Launch was through Spruce `standard_launch.sh` after the exact MainUI generation was stopped. A fresh read-only check shows MainUI restored, no Infernax process, no port launcher, and no pending `/tmp/cmd_to_run.sh` handoff.
- Installed binary and backup hashes still match. The run ended with exit 21; no stable game launch or menu is established.

## Device-side evidence
- GLES: Mali-G57, OpenGL ES 3.2, EGL 1.4, texture limit 16383, fragment sampler limit 128.
- UBO capability probe: `max-block-size=65536`, `max-bindings=216`, `vertex-blocks=36`, `fragment-blocks=36`, `combined-blocks=216`. This is capacity only, not proof of a working UBO path.
- The runtime reported 22 identical errors: `NVN_UNIFORM_ARRAY_GL_ERROR bytes=65536 vectors=4096 error=0x502` (`GL_INVALID_OPERATION`).
- First sampled present: `NVN_PRESENT_PIXELS frame=1 nonblack=0 max=0 white=0 texture-draws=1 status=BLACK-SAMPLE`.
- Final stop: `STOP status=NVN_RECEIPT_DIRECTORY pc=main.text.seg+0x83596c detail=0x11 query_memory_calls=0x856 tls_trap_reads=0x0`, followed by `exit=21`. The code passes `errno` as `detail`; `0x11` is `EEXIST`.
- The port directory contains `nvn-video-1/group-0.jsonl`. `port/Infernax.sh` currently defaults `COTM_RUN_ID` to `1`; the principal payload did not set a run ID. This makes reuse of the existing receipt directory the leading explanation for the EEXIST stop, but the exact run ID was not printed in this run.
- The log contains no `NVN_BIND op=nvnCommandBufferBindUniformBuffers`, `NVN_CBUF`, or `NVN_DRAW_UNIFORM` records, so actual cbuf binding-slot provenance is still unverified.
- The two reviewed KMS captures were taken after the game process exited and showed the Spruce idle clock/screensaver. They are not proof of an Infernax frame or live scanout; an in-run KMS capture is still required.

## Code-path facts / unresolved direction
- `tools/build_catalogue.py` rewrites the cbuf declaration to `uniform highp sampler2D <name>` and indexes it with `texelFetch`; its active ESSL rewrite comments specify a plain RGBA32F texture because buffer textures are unsupported.
- The generated shader header contains `sampler2D c3`/`c3_1` cbuf declarations.
- Runtime metadata marks the 65536-byte array as `kind==7`. `native/nvn_render.h` allocates `u->size` bytes, reads the bound range, then calls `glUniform4fv(location, n/16, words)`; the measured 4096-vector call fails with `0x502`.
- The current skill references conflict: `essl3-catalogue-and-fbdev-present.md` documents an RGBA32F `sampler2D` upload with a dedicated unit/object and end-of-loop audit, while `uniform-limits-and-null-handles.md` says UBO or buffer texture is the only route. The current emitted GLSL already uses `sampler2D`; selection and correct slot mapping still need evidence.

## Status
No UBO implementation, no cbuf texture upload, no in-run screenshot, and no game menu yet. Preserve old receipts; do not delete `nvn-video-1` to bypass the collision. Next launch needs a unique numeric `COTM_RUN_ID` and a live capture while the game process is running.