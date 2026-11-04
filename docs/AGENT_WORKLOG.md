# AGENT WORKLOG — Infernax TSPS
## Objective
Continue the existing native Switch Horizon/NVN port until a real structured Infernax frame is captured; no diagnostic fill counts as success. Consult ChatGPT for each material phase.

## Confirmed findings
- Resume 2026-11-04 19:30 +04: TSPS 192.168.50.135 is reachable over a fresh non-multiplexed SSH connection. One runtime->principal->PyUI/MainUI chain; no target process or pending handoff.
- Installed build 0083 matches local `build/out-all-83/infernax-nextos`: SHA-256 `3ff2973109558e5f235f95e5a9d530c91a5bd51367d60ed7e9635c335a0b7da1`. Launcher SHA `dd898a8b8df342d05763cf20cc3ee91423dbe281df8b1295c3af4dda88956d6c`.
- Last preserved device log is `build/resume-20261104-1930/previous.log`, 922608 bytes, SHA `3828abc7496cf903840b305e4bc56e83b6706b5e7aeb61869e360a4a5ee5d2b7`. Last run reached only 2 black presents and STOP T2_MEMORY_FLOOR; do not reuse the historical 340-540 frame CPU_FAULT count as this run's result.
- Source audit: `nvn_cbuf_refresh()` binds and uploads each vertex bank, then unbinds that very texture before draw; thus previously verified upload/bind logs do not prove draw-time sampler state.
- Source audit: refresh reads declared 65536 bytes rather than short bound sizes (48/64/384 bytes), unlike the zero-padding initial upload. Its slot cache persists across programs.
- Generated catalogue currently maps the first `new` VS c4/c5_1 to bindings0/1 while live vertex ranges begin at 1/2 for that draw. Shader cap9-1 includes c3/c4/c5 ->0/1/2. Descriptor declaration index is not yet proven as NVN slot.
- `chatgpt-use` 0.0.12 and extension relay are responsive. Existing conversation's composer readback is Thinking effort / High. Old stage6 answer fully read from scratch/cgu6.out.

## Addresses & offsets
Baseline guest update image is unchanged. VS cap9-1 uses model c5_1[0..15] then projection c4_1[0..15], c3[0..3] for UV. Raw shader text extracted to build/resume-20261104-1930/shader-*.glsl.

## Files modified
Pre-existing dirty runtime/infernax-adapter.patch preserved as build/resume-20261104-1930/starting-0083.patch. Scratch source /var/home/ruslan/.hermes/cache/scratch/infernax-ubo-tree retains build0083 overlay.
New local-only TASK_SPEC.md and this worklog; no new runtime edits yet.

## Experiments
Historical shifts +/-1 and matrix overrides are not new evidence and must not be repeated blindly. Probe flags compared by value. Uniform magenta FF00FF was diagnostic, explicitly rejected as first light.

## Build & test results
No new build/deployment/run yet in this continuation. Current device binary0083 hash verified above. Current idle MemAvailable683384kB, CmaFree111480kB; previous run memory wall remains unresolved.

## Blockers
No verified game frame. Must first correct/measure draw-time texture-binding and bound-range contracts. Sampler metadata and native CPU_FAULT are separate unresolved frontiers.

## Next actions
1. Ask ChatGPT to review exact source audit; preserve response and receipt.
2. Bound the first device arm by independent target/controller deadline; use exact current launcher and ordinary principal path.
3. Test one fix at a time, preserving baseline and collecting draw-state, PPM and KMS with owned process identity.

## Repo state
Branch main HEAD561897acc243a4cff1ad8436afe8c6524f474835; remote main matches. Original dirty overlay was preserved; TASK_SPEC.md/worklog are local-only. No commits/pushes yet in this continuation.

## Continuation checkpoint (after stages7–9)
- Device reachable; real MainUI/principal ancestry and clean menu screenshots repeatedly verified. Controller identifies guest by /proc/exe (comm is actually terminator2d-ne), not pidof infernax-nextos. Never manually start MainUI.
- Evidence root build/resume-20261104-1930; per-arm JSON configs, bounded run_controller.py, original and final logs, /proc PID/starttime/resource samples, KMS captures and cleanups are saved there. Remote diagnostics at /mnt/SDCARD/Roms/PORTS/infernax/diagnostics. Baselines83/84/85 preserved with SHA.
- Stage7 consultation completed request consult-infernax-stage7-20261104-1930, conversation6aeb5156-c270-83eb-9cca-42649a94672a. CLI extracted only final sections; full page saved consult-stage7-page.txt. Stage8 completed consult-infernax-stage8-20261104, conversation6aeb54c8-334c-83ed-8bd8-951d986bf5e1, answer consult-stage8.out.
- 0083 refreshoff armA run1793807110:4black presents, queries0, floor32MiB exit26; refreshon armB1793807261:2black presents, queries0, floor exit26; live KMS1280x720 allRGB0. SIGUSR1 reached verified PID; black PPM is refused by nvn_video_capture_ready(!proof), not evidence of a missed signal.
- 0084 audit-only SHA341d7b1859bd992965e668a2a07e44192574aeab68f036b596739086840a5947; armC1793807352. Proved vertex bank textures nonzero before refresh ->0 at draw, same-name VS/FS c3 both loc2 and sampler unit overwritten, all ordinary sampler textures0.
- 0085 transport-only SHA6c8ed4cc3a93a4665142948d5f7327152734bb2f9134f24e193cfd9fd9c62d5b; armD1793808005. Removed redundant global refresh (the existing kind7 uniform loop ALREADY runs per draw), activated correct unit before creating a bank, bounded live copy+NULL guard. All44 cbuf pre-draw readbacks nonzero. Still2black frames and floor exit26. Numeric exponent conversion intentionally retained for isolated test.
- 0086 generator-only SHA6d6b7231331b236b4795c393f435ca75ad9b5b3b235fdd2f81b346d7d0b0fdbe; armE1793808134. Native render source byte-identical85/86. Generator now derives application slot from original Maxwell cN (N-3 for3..18), retains raw_bank/spirv_binding; namespaces VS/FS cbuf names; maps reserved rawc2 samplers to adapter binding14 and offset-0x20. Restricts literal inlining to rawc1.8 generator regression tests passed (previously7fail); all15 shader identities/order unchanged; GLSL identical after undoing stage namespace.
- 0086 stops BEFORE first draw at ZERO_FLOAT_TEXTURE_SAMPLER_BACKEND_REQUIRED format5/R16F,exit20. Real sampler routing now reaches128x1 R16F LUT/default textures IDs112..127 previously silentlynull. GLES2 OES extension-only guard falsely rejects GLES3.2 core texture support. No frame yet. Clean target/wrapper/handoff and actual Spruce Recents screenshots verified for all arms.
- Currently INSTALLED0086, launcher stilldd898a8b8df342d05763cf20cc3ee91423dbe281df8b1295c3af4dda88956d6c. No active target run; ordinary menu returned afterE.
- Stage9 consultation running request consult-infernax-stage9-20261104, conversation6aeb57e1-8190-83ed-9766-3f2c476b8884; systemd unitcgu-infernax-stage9, files consult-stage9.md/.out/.err. Ask was core ES3 half-float capabilities, core0x140b token, sizedRGBA16F, no quantization or memory changes.
- Scratch now has UNBUILT candidate87 core-float patch in nvn_texture_formats.h,nvn_textures.h,nvn_cpu_packed.h,zero_mip_clamp_runtime.h,nvn_render.h. Pure decoder tests are in tests/test_core_float_contracts.py (3baselineRED, candidate verification pending at checkpoint). No87 build/deploy. Existing runtime/infernax-adapter.patch still85; must regenerate AFTER source review before next build. Current runtime shader header is86 generated catalogue.
- tests/test_native_cbuf_contracts.py compiles actual upload block:3transport tests pass85, raw-int-word test intentionally stillfails (heuristicconversion retained). Its baseline failure prints whole source via assertNotRegex; change to concise regex assert. New tests/test_catalogue_contracts.py and tools/build_catalogue.py are uncommitted.
- Tool CLI has NO --reply-to argument in0.0.12; an attempted stage8 call rejected before submission, then rerun correctly positional prompt and --file. Do not use nonexistent flags.
- A preflight-only attempt with wrong expected standard_launch hash rejected before any TERM. Actual file hashfb28cb735c547f6c2411b85c8f0b4989d04e6f9ba5ff8c072cf418af31c2a1fd; derive hashes programmatically, no guessed strings.

## Current checkpoint — 0090 routing verified, no structured frame

Latest user steering: «Чини дальше». The archive request above was completed separately; the third requested reference was CotM, not muOS. Older installed/pending statements in this file are historical, not current state.

- Durable evidence/source checkpoint: `/home/ruslan/infernax-repair-20261104`. Former repo build workspace disappeared; image regenerated from owner inputs and matched all 13 installed segment/input SHAs. No credentials stored here.
- 0087 fixed core GLES3 half-float policy/token and created 16 R16F allocations with GL error0. 0088 instrumented independent separate texture/sampler writes; 0089 captured missing matched shaders. All15 exact code+control pairs recovered; isolated reflection compiler emits byte-identical GLSL/SPIRV and22 sampler descriptors. `reflect_pinned_shaders.py` now includes control_sha256 as required by strict generator identity joins.
- Stage12 explicitly GO for one routing-only0090 after CPU regression checks, no generic sampler0 fallback. Source metadata retains separate secondary CB operand0x568; separate textures/samplers have independent state and written masks. Actual source strings and all15 code/control pairs/order byte-identical catalogue86. Generator regenerated all22 descriptors; three blank-line-only source changes replaced with the exact baseline strings.
- 0090 installed SHA `bbcba13a21ef6027783daf9ef432c1c68a50c25df2104ecc231543523e44cfd5`; source/compiled input comparisons passed. Host21 tests:20PASS, one deliberately deferred RED `test_integer_looking_words_are_not_numerically_rewritten_on_cpu` (0x00000001 incorrectly becomes0x3f800000). No skips. Runtime adapter patch and catalogue synchronized, no new commit/push.
- ArmI run1793813978, pid10683/start1884518, principal-only, capture-all0, same floor32/clocks. Live receipt: fs_tex2_170 texture-slot1 + sampler-slot0 -> texture0xa60/sampler0x160 -> handle0x16000000a60; all required slots resolve without missing-operand STOP. Routing acceptance proven, not image acceptance.
- First present is exactly one blue RGB(0,0,188) fill across1280x720 (runtime PPM `nvn-frame-ppm-1793813978-1.bin.gz`). KMS live0 allRGB0, second present black; no structured game frame. Scene draws1..4,7,8 still occlusion0; LUT/fullscreen draws0,5,6 have samples1. Stops at memory floor31MiB/floor32, exit26, VmHWM821184kB, stopRSS529264kB/swap309796kB. Texture budget last printed266MiB is not complete memory attribution.
- Host evidence retrieval originally matched only `.bin`, missing compressed `.bin.gz` PPMs. First present was recovered independently; future retrieval must include both suffixes. The device controller itself does not copy dump files. This was an evidence-collection defect, not a missing runtime capture.
- Cleanup verified target/wrapper0, handoff absent, CPU0..3 original0/mode0444 restored; KMS cleanup shows actual Spruce Recents/Infernax selection. Device remains0090; 0089 preserved as `infernax-nextos.before-90` with SHA readback.
- Stage13 request `consult-infernax-stage13-20261104`, conversation6aeb6eb9-ca78-83eb-b7f2-334df93cfc5e. Proposed next0091 only removes exponent-based CPU word conversion; memory/graphics changes must remain separate. Initial prompt lacked recovered compressed PPM, so correct that fact in followup: first present is a proven solid-blue fill, not unknown image content.
- Reproduction artifacts: `candidate90-adapter.patch`, `catalogue90.h`, `catalogue90-work`, `owner-shaders`, `sampler-reflection.json`, `out90`, `arm-i.json`, `arm-i-controller.jsonl`, `arm-i-90-separate-routing-1793813978`, `checkpoint90-manifest.json`.

## Latest checkpoint — stages13–16, 0091 installed / 0092 built

- 0091 deletes exactly the 4-line exponent-conversion/comment block; raw CPU bits now preserved for integer-looking words, -0, subnormal, NaN, infinity. All22 tests PASS. SHA `b648de9fd03956bec43520f3e8bcdc42bf020f8259abc515bf21120d7f524388`, shader/catalogue bytes unchanged. Installed with90backup/readback; armJ run1793814503 gives same solid-blue present1, black present2, floor31MiB/exit26. No structured frame.
- Stage14 approved one unchanged0091 geometry/attribution armK run1793814903. Captured real four floatXYZ vertices and actual c4/c5 data for first four scene-pair2/0 draws. Float32 CPU oracle (`arm-k-geometry-oracle.json`) proves each complete set lies outside/on one common clip halfspace (bottom/right/top/left respectively), w1 and finite. Therefore no positive-area interior triangle is possible regardless of unknown index order/culling. Those zero queries are expected; likely external letterbox masks, intended purpose not proven. Do not flip culling/depth/projection to show them.
- ArmK observer failed calling nonexistent ctrl.proc_status after copying startup smaps; JSONL empty. This is explicitly incomplete, not peak evidence. Corrected `observe_memory.py` uses direct proc reads and bounded8 samples/2fullsmaps, exact controller PID/start/exe/run checks. Host test proves2validsamples, injected smaps error preserves prior journal, and spawned-own-child disappearance detection. `observer-self-test.json` records assertions.
- Stage15 approved one corrected attribution-only armL run1793815318, exact0091. Collected8 samples/2smaps; atpressure crossing fullsnapshot completed after MemAvailable recovered193492kB. Disjoint ownership atthat snapshot RSS544068kB: candidate guestalias nonreportedpool211848, fullycoveredpool106216, mixedpool540, nativeanonymous117800, file98960, hostheap8372, Mali/shared332. Pools conservatively all22ever-bound ranges, nofinalize lifetimes; no coldness or actualcompressibility proven. `arm-l-memory-ownership.json` contains exact Size/Rss/Pss/Anonymous/Swap/SwapPss; never sumglobaldma withRSS. GZRAM staysOFF.
- ArmL reached218 presents, allunstructured: present1 bluefill, restblack. Different lifetime not improvement claim. New terminal failure `HOST_THREAD_AFFINITY_FAILED detail0x16/EINVAL exit21` at worker entry sdk.text+0x12fb00, notmemoryfloor. Online0..3 at exacterror unknown; idle4..7 doesn'tprovefailurecause. MainUI absentduringrun; cleanup targets/wrappers0/handoffabsent/cores restored; actual Spruce Recents confirmed forJ/K/L.
- Stage16 `consult-infernax-stage16-20261104`, conversation6aeb7441-de84-83ed-b803-859ebbb78a34: GO0092error-onlyreceipt, conditionalGO0093ONLYif requestedmask hasnoonline/allowedintersection andphysical4..6 available withvalidthreadidentity. No globalVM/clocks/GL/floor/launcherchanges. If receipt insufficient, noCPUremapping.
- 0092 source adds only post-error reads/logs to thread_affinity; preservesSTOP21. Prints sequence/monotonic/currentTID/pthreadself/native/gueststate/mask/requestedhostmask, sysfsonline, targetTID-safe sched_getaffinity, taskstatus/cgroup/rootcpuset. Does NOT dereference potentially stale targetpthread for diagnostics. Real helper Ctest is RED on91 andGREEN on92, validates guestguard and retainedEINVALSTOP. Full23 tests PASS. Built SHA `90a594fe4a7434e586179bed19c64d5d6af06d5843aa0481c96bf116ef914b1d`, exported `candidate92-adapter.patch`/runtimepatch; catalogue unchanged. NOT YET installed at this checkpoint. Device remains0091.
- If0093 is justified, map host mask only, leave guest core/mask/sentinels logical; cover worker helper AND loader mainpin, plus verify guest-visible currentCPU doesn't leak physical4..6. Preserve all unrelated successful graphics/audio paths and exact cleanup. No arbitrary fallback onEINVAL.
- Durable files now include out91/out92, candidate91/92 patches, tests91/92 logs, armJ/K/L configs/controllerlogs/evidence, geometryoracle, observer andownershipreport, consultations13–16. No commit/push in this continuation.

## Stage 17 (2026-11-04): floor was the progress wall; two independent blockers isolated

Installed binary unchanged (0092). Arms n/p/q/r/s, all through the principal controller, cleanup verified (real Spruce, no target/wrapper/handoff, cores restored).

- `arm-n` (`MOMO_TRACE_DRAWS=200`): draw→target chain is correct — scene draws go to target 130, composite pair 14/3 writes target 2/3/4, and exactly that id is presented. Frame 1 presents `nonblack=921600 max=188` (full screen); every later frame `BLACK-SAMPLE`.
- `arm-p` (`NX_FORCE_STATE=1`): with cull, scissor, depth and blend all disabled the four scene quads still yield `samples=0`. **Cull/scissor/depth are not the cause**; those draws do not rasterize.
- `arm-q` (`T2_MEMORY_FLOOR_MIB=8`): reached **194 presents** with memory *stable* — `MemAvailable` 58–89 MiB, `VmRSS` ~700 MiB, `VmSwap` ~500 MiB. The 32 MiB floor was firing on the transient load hump only; the title does fit after loading. Stopped at frame 194 with `HOST_THREAD_AFFINITY_FAILED detail=0x16 (EINVAL)`.
- `arm-r`: 221 presents, same affinity stop. At frame 100 pair 2/0 draws **unit quads** (`-0.5..0.5`, UV 0..1) with a 4×4 texture into target 130; presents still black.
- `arm-s` (`NX_PROBE_FS=1`): the probe program does **not link** (`NVN_PROBE_FS compile=1 link=0 err=0x501/0x502`), so that arm is inconclusive — the probe path itself needs repair before it can be used as evidence.

New root cause with receipt (`0092` diagnostics):
```
INFERNAX_AFFINITY_ROOT_CPUSET path=/sys/fs/cgroup/cpuset/cpuset.effective_cpus value=4-7
INFERNAX_AFFINITY_ROOT_CPUSET path=/sys/fs/cgroup/cpuset/cpuset.cpus value=4-7
```
`thread_affinity()` builds the host mask from guest bits 0..2 → host CPUs 0..2, which are outside the effective cpuset at that moment, so `pthread_setaffinity_np` returns EINVAL (22) and the fail-closed stop fires. This is the stage16 conditional-GO case: map guest 0..2 → host 4..6, keep `t->core/t->affinity` in guest space, apply only when building `cpu_set_t`, and cover both `thread_affinity()` and the loader's own `sched_setaffinity()`.

Next: 0093 mapping (removes the ~200-frame ceiling) and, in the same or the next build, replace the broken `NX_PROBE_FS` link path so a magenta arm can separate "does not rasterize" from "fragment output is black". No picture yet.

## 0093 / 0094 / 0095: affinity mapping fixed, guest transform decoded

- **0093** (`T2_HOST_CPU_BASE`, SHA `2d307f…1d73e`) maps only the *host* mask (guest core/mask untouched, default 0 = identity). arm-t2 (base 4, floor 8): **223 presents, no `INFERNAX_AFFINITY_FAILURE`, no EINVAL** — the ~200-frame ceiling is gone. New stop is the older `CPU_FAULT pc=0x…e0d0 detail=0x62a8/0x67ae exit=22`.
- **0094** adds `MOMO_ATTR_UNI` (the matching draw's uniform banks). 0095 widens it to 160 bytes. 23 host tests still green.
- Decoded pair 2/0 at frame 100 (draws 595-598, target 130):

```
bank1: 1/640, 0, 0, 0 | 0, -1/360, 0, 0 | 0, 0, -0.0010003, 0 | -1, -1, 0.90027, 1
bank2: 4000,0,0,0 | 0,4000,0,0 | 0,0,1,0 | 640,2000,-98,1      <- model (tile 0)
       0.00025,… | … | 0,0,1,0 | -0.16,-0.5,98,1                <- inverse
       0,0,-nan,0 | 0,0,0,1                                     <- NaN in the inverse's z column
```
  Vertex `(-0.5,-0.5,0)` through the model gives `(-1360, 0, -98)` — exactly what the geometry oracle measured at frame 1, and the inverse already reproduces `-0.5`, so the guest's own math is coherent and our uniform transport is byte-faithful.
- Consequence: the quad is 4000×4000 at `(640,2000)`, which in this ortho covers clip x −2.1…4.1, y −11…0, z ≈ **+0.998** (right at the +1 plane) — i.e. it should cover at least the top half of the screen, yet occlusion says `samples=0`.
- Two open candidates, both measurable: (a) z sits at the clip/`w` boundary and is being culled by the z plane, (b) the uniform bank carries a **NaN** (float index 34) that poisons whatever the VS derives from the inverse matrix. Neither is a cull/scissor/depth issue (disproved with `NX_FORCE_STATE=1`, cull=0/scissor=0/depthtest=0 for all draws while 1-4 still gave `samples=0`).

## 0096 / 0097: the black screen is a set of 4x4 textures that never leave zero

`NVN_DRAW_PIXELS` (first 8 draws) already told us more than the occlusion counters:
- draw 1, 1024x32, program 1 → `max=253 px=0,132,128,255` — the draw path, FBO (`status=0x8cd5`), viewport, write mask and texture-unit binding are all fine.
- draws 2+, 1280x720, program 2 → `max=0 px=0,0,0,0`.
The earlier `samples=0` reading is therefore **not** evidence of missing rasterization (draw 1 rasterizes while reporting 0) — the occlusion counter is misattributed and must not be used as a stop/go signal.

0096 adds `MOMO_ATTR_TEX`: program 2's only sampled texture, `fs_tex2_168=id36/4x4`, is **black** (`px0=0,0,0,0`, FBO complete).
0097 adds `MOMO_TINY_TEX` at the upload site — and the guest's own bytes are zero too:
```
MOMO_TINY_TEX id=34 4x4 fmt=0x25 bpp=4 stride=16 guest=0,0,0,0 decoded=0,0,0,0
MOMO_TINY_TEX id=36 4x4 fmt=0x25 bpp=4 stride=16 guest=0,0,0,0 decoded=0,0,0,0
MOMO_TINY_TEX id=40 4x4/2x2/1x1 … guest=0,0,0,0        (same for 37, 41, 42)
```
So the upload/decode is faithful — the snapshot at creation time is genuinely black. These are the SDK's solid-colour atlases; the guest fills them later (CPU write into the pool or a render into them) and our GL copy is creation-time only. Open question for the next step: are they `b.target != 0`, and does the `zero_texture_writes`/view-generation refresh path cover a 4x4 RGBA8 written after creation?

## 0098: negative result, the 4x4 textures do not resolve to a live pool

`MOMO_ATTR_TEXG` (guest bytes at draw time for the sampled texture) printed **nothing** for `id36` — `nvn_texture_find(36)` returns NULL or its `pool` is NULL, even though `MOMO_TINY_TEX` logged the same id at creation *with* a pool address (`guest=0,0,0,0`, `stride=16`). So either the object was recycled/finalized between creation and frame 100, or the small ones take a path that leaves `pool` unset.

That also reframes the earlier datum: the zero bytes in `MOMO_TINY_TEX` are at creation time through `pool->address+b.offset+levels[0]`; the texture that program 2 samples at frame 100 is the same object id but no longer carries that pool. Next probe must therefore resolve the sampled texture by GL name (`nvn_textures[i].name`) instead of by guest id, print `live/packaged/packaged_uploaded/target/pool`, and read the guest bytes through whatever storage the struct actually has.

Also noted: `NVN_COPY_STALE_OBJECT` count is 0 and `NVN_COPY_BUFFER` is 0 in this run, so no copy is being dropped as stale.

## 0102–0108: the 4x4 atlas is stale, and after fixing it the frame is still black

**Fixed (verified on device), 0108 = `b9fa845c…5b7f`:**
`nvn_texture_small_reload()` (nvn_textures.h, called from the sampler bind right after `nvn_cpu_image_refresh`) hashes the guest rows of every ≤64x64 single-level texture and re-uploads it with `glTexSubImage2D` when the bytes differ from what we last pushed. Infernax's atlas lives in an uncached pool, so no `FlushMappedRange` ever marks `zero_cpu_dirty` and the sampled image kept the all-zero snapshot from creation.
Evidence:
```
T2_TEX_RELOAD id=36 name=5 4x4 fmt=0x25 first=255,255,255,255
T2_TEX_SAMPLED id=36 name=5 4x4 status=0x8cd5 px=255,255,255,255   (was 0,0,0,0)
```
Two failed placements, for the record: calling the reload from the `zero_texture_refresh` site (nvn_render.h:385) hangs the guest in init (`STOP status=TIME_LIMIT`, zero draws) because that path is not the GL-owning one; the sampler bind at the `nvn_cpu_image_refresh` line is safe.

**Still black, and now a different question:** with the sampled atlas provably white, `NVN_DRAW_PIXELS number=2` is still `max=0`, and the live screenshot is `(0,0,0)` on all channels. So the black is the *fragment output* of program 2 (pair 2/0), not the texture: next probe is its cbuf/uniform colour, plus a forced-FS-colour draw to prove the draw writes anything at all.

**Tooling blocker:** chatgpt-use refuses to run ("could not find the composer model picker: composer toolbar not found") for both `high` and `current`, so the per-step consultation the user asked for could not happen this session.
