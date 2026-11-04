# Infernax — TSPS (NextOS/NVN) port

Bring-up of **Infernax** (Berzerk Studio) on TrimUI Smart Pro (SpruceOS) through the
native Horizon/NVN lineage runtime used by the Terminator 2D / Lamentum / WoL ports:
the game's own Switch IL2CPP ARM64 modules execute on a reimplemented Horizon layer
that translates NVN to GLES.

Status: **modules map, SDK init runs, first NVN program reached** — the runtime stops at
`ZERO_SHADER_NOT_PINNED` because no shader catalogue exists for this title yet.
Not playable.

## Owner content (never committed)

Extracted from the user's own dump:

| Input | Title ID | Version |
|---|---|---|
| Base | `010039A016792000` | v0 |
| Update | `010039A016792800` | v327680 (**required**) |

The port uses the **merged base + update** content, as a console with the update
installed would (update RomFS is BKTR-patched, so `data.unity3d` and
`resources.resource` really differ from the base).

- Engine: **Unity 2020.3.36f1** (update; base is 2020.3.18f1), IL2CPP, no `Managed/*.dll`.
- SDK: **14.3.2.0**.
- Modules (update): `rtld`, `main` 47,090,436 B text, `subsdk0`, `sdk` 6,145,344 B text.
- NPDM: Flags0 `0x37` (64-bit, AddressSpace64Bit), SystemResourceSize 16 MiB,
  main stack 1 MiB, priority 44, core 0, 47 requested services (`nvdrv`, `vi:u`, `hid`, `fsp-srv` …).
- RomFS section: 214,932,268 B = 0x200 zero header + RomFS image,
  SHA-256 `2ab01319a6a96e8392ce2951ffc3d30798392e38845ef1ce2df46ea2cc3711da`.

## Measured bring-up (2026-11-04, device 192.168.50.135)

```
MAP rtld.text.seg base=0x8000000 ... sdk.text.seg base=0xcd65000 size=0x5dd000
ENTER original rtld; bridges=127; no manual relocations; guest TLS virtualized
... query_memory_calls=0x97, 26 guest threads
STOP status=ZERO_SHADER_NOT_PINNED pc=main.text.seg+0x876950 detail=0xb7b9200
```

Two stops were passed on the way:

| Stop | Cause | Resolution |
|---|---|---|
| `T2_LAZY_MESH_SEGMENT` | baseline's T2-only lazy-mesh patch pins T2's `main.text` size | run with `T2_LAZY_MESH=0` |
| `ZERO_SHADER_NOT_PINNED` | first NVN program; catalogue absent | open — shader translation |

## Build your own copy

```sh
# 1. owner image (segments + generated contracts + RomFS section); needs python lz4:
python3 tools/prepare_infernax_image.py <merged-exefs-dir> tools/npdm.json tools/romfs.json build/image

# 2. cross-build (container ships aarch64-linux-gnu-gcc 9.4):
docker run --rm --security-opt label=disable -e COTM_CC=/work/tools/cc-infernax.sh \
  -v <baseline>:/reference:ro -v <image-dir>:/image:ro -v "$PWD":/work \
  --entrypoint /bin/sh pmjs-arm64-spruce-local:20261002-gcc10 \
  -c 'cd /work && bash build_infernax.sh /reference /image /work/build/out'

# 3. install: infernax-nextos + *.seg + infernax-romfs.section in one directory,
#    run it from that directory (the runtime opens the section relative to CWD).
```

`tools/cc-infernax.sh` only downgrades `unused-result`/`unused-function` to warnings and
sets `ZERO_ZX_GAME=1`; the baseline source stays read-only apart from the adapter overlay.

## Not done yet

- Shader catalogue (per-program NVN→GLES translation) — the current frontier.
- Title rebinding of the baseline's T2D identity strings/banner and the SDK generation
  layout defines (`MOMO_SHADERS=1` selects SDK 18.x layouts; this title is SDK 14.3.2).
- Audio, input, saves, gameplay, performance.
