# 0170 — QEMU System ARM64 evidence

## User-directed gate

Reach a real game menu in a full ARM64 VM on AOOSTAR first. Only then deploy and
test on TSPS; no device mutation is part of this VM phase. Do not qualify Mali,
physical controls, audible output, handheld RAM or game speed from VM results.

## Provisioning verified

- PM-Porting-Tools `f41854f08121823020e603325f01ddfff8509048` inspected. It supplies
  qemu-user/Xvfb scripts; full-system VM provisioning is in `tools/qemu-system/`.
- Rootless QEMU **7.2.22**, TCG Cortex-A53 x8, Debian 12 ARM64, guest kernel
  **6.1.0-54-arm64**, 8 GiB RAM, 24 GiB private qcow2 overlay. Host cap:
  6 CPUs / 12 GiB. No KVM cross-architecture claim.
- Official generic ARM64 cloud image: 437649408 bytes, SHA-512 verified before
  creating its overlay. Original base image remains untouched.
- Initial QEMU start failed on an absent optional virtio-net PXE ROM. The exact
  failure log was retained; disk boot works with `romfile=`. Running QEMU state,
  serial Debian boot, key-authenticated guest SSH and `cloud-init status: done`
  were independently read back.
- Xorg/Openbox **1280x720**, virgl renderer **AMD Radeon Graphics (gfx1103_r1)**, host GL4.6,
  guest GL4.3 and **OpenGL ES3.2 Mesa22.3.6**, matching loaded SDL2/EGL/GLES/ALSA
  dependencies. `SDL_VIDEO_X11_FORCE_EGL` exists in the guest SDL library.
- Original out166 runtime SHA-256
  `258a53591bdb77d7a41a92be2a7453fb4e4b8ee1282754a5aa5607fa383cbe86`;
  **13** private segment/RomFS files independently match the prior baseline.
- PM uinput pad and `compare_frames.sh` reused in the guest. Identical fixture:
  AE=0, `0 frames differ`; changed 1280x720 fixture: AE=921600,
  `1 frames differ`. These diagnostic fills qualify capture/diff sensitivity,
  **not game graphics**. Kernel uinput/joydev and virtual HDA device are present;
  audible output is not tested (host audio backend discards output).

## Baseline bring-up — do not mislabel harness failures

- First wrapper invocation incorrectly requested a 1200-second runtime budget;
  production validates 30..900 and refused. Corrected VM-only budget: 900 seconds;
  handheld budgets are unchanged.
- Next invocation used a RomFS symlink; production O_NOFOLLOW verifier refused
  it before guest execution. The VM wrapper now copies the complete regular file
  to its private work directory and rechecks its SHA-256. No verifier was weakened.
- Run `0170a-1793874384` then reached the **same nvdec frontier**: opens ctrl then
  nvdec, `STOP status=UNIMPLEMENTED_NV_DEVICE`, exit20, maximum present899,
  wrapper elapsed210.887594272s. Exact game/pad child groups were cleaned up.
- Its capture observer fired on the initial undefined surface/black frames.
  Inspected captures show only a blue-grey surface and black, not a warning,
  logo or menu. Positive runtime readbacks around present600 are **not** external
  game-frame qualification. This run alone fails the visual baseline gate.
- Observer self-checks now reject first undefined/black samples and accept a
  positive sample after multiple texture draws. Corrected unchanged baseline
  `0170a-1793874898` completed in 218.464469180s, exit20/no timeout, max
  present903 and the same ctrl -> nvdec -> UNIMPLEMENTED_NV_DEVICE frontier.
  Independent X11 captures `content-0.png` and `content-2.png` visibly show the
  mature-content warning at 1280x720 (dark fade and full brightness), **not a
  menu**. Exact game/pad children exited. This satisfies the 2D visual baseline
  gate; it says nothing about decoded-video presentation.

## Separate VM-only candidate — not deployed

Consultation `infernax-0170-qemu-software-omx-c9fb107`, High/current, was read in
full. It gave GO for baseline and conditional GO for the single-instruction VM
candidate **only after** a visual baseline: code reaches OMXMaster registration,
real software plugin/AVC implementation exists, and normal 2D presentation works.
It requires ABI/callee-saved state, registry/destructor ownership and potential
shared OMX_Init dependencies to be checked; no claim of safe final software
fallback follows merely from static symbols.

`stage-software-omx.py` fails closed on full original segment SHA-256 and
17 surrounding ARM64 words. Only subsdk0 text offset0x272c34 changes
`67fa0294` (BL operator new) to `06000014` (B0x272c4c), skipping allocation,
construction and registration of NVIDIA OMX, while retaining actual SoftOMXPlugin
construction/registration. `clearPlugins` iterates the registered list and
`addPlugin` records the specific passed plugin; software factory statically
calls its compiled-in creators. Shared transitive OMX_Init effects remain an
experimental risk, not disproven by this ownership check.

Positive local self-check produced candidate SHA-256
`2ff853a8335902c685e8b3d981e11d9d597c86a68e3e8ad6daa7eb2f8d579366`;
exactly four differing byte indices, length and branch target checked. Three wrong
inputs were rejected. Staging is exclusive to a new sibling `candidate0170`
directory; source bytes and original saves are not writable through this action.
No decoder selection/decode/menu success has yet been claimed. Do not replace
real decoding with fake nvdec success, removed media or reduced-quality assets.
