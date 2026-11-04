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

## 0109–0113: the scene draws write nothing at all

`NVN_DRAW_PIXELS max=0` was never "the draw wrote black" — it was the black clear surviving. Pre-clearing the target to red (`NX_DEBUG_CLEAR_RED=1`) proves it:

```
number=1 px=255,0,0,255   -> red survives (draw 1 is 1024x32 program 1 in the earlier numbering)
number=2 px=255,0,0,255
number=3 px=255,0,0,255
```

With red in front of them, the program-2 draws change no pixel. Ruled out in the same run, one variable at a time:
- culling (`NX_NO_CULL=1`): still nothing written, so it is not a winding/cull mismatch.
- depth (`NX_NO_DEPTH=1`): still nothing written, so it is not `GL_LESS` against a 0.0 depth clear.
- viewport/scissor: `NVN_DRAW_RECT` shows `viewport=0,0 1280x720`, `scissorbox=0,0 16384x16384` — not clipped away.
- geometry is submitted: `NVN_DRAW_GEOM prim=0x4 count=6 indices=1`.

What is left is that the vertex stage collapses the triangles: the transform program 2 reads is zero/wrong (cbuf or UBO binding) rather than the matrix the oracle computed. Next probe: dump the actual transform constants bound for program 2 at its draw, not the ones we believe we sent.

## 0116–0120: not MRT, and the two draws carry different transforms

- `NVN_DRAW_BUFS`: one attachment (`att0=0x1702/79`), `read=draw0=GL_COLOR_ATTACHMENT0`, no MRT — that hypothesis is dead.
- Draw 1 and draws 2+ share the same program (2), the same FBO attachment (79) and the same attribute layout (`[0 sz=3 st=48] [1 sz=2 st=48]`), yet only draw 1 writes. So the difference must be per-draw constants.
- `MOMO_UNIFORM_DUMP=2` gives draw 2's constants:
```
vs_c4_1 (64B): | 0.0015625 0 0 0 | 0 -0.0027778 0 0 | 0 0 -0.001 0 | -1 -1 0.9003 1 |
vs_c5_1 (384B): | 4000 0 0 0 | 0 4000 0 0 | 0 0 1 0 | 640 2000 -98 1 |
```
  and draw 1's, which does render, is `| 2 0 0 0 | 0 -2 0 0 | ... | -1 1 0.99 1 |` (NDC-space quad).
- Reading those two: world → ortho maps `y` through `-2/720` with `ty=-1`, so world `y∈[2000,2720]` lands at NDC `[-6.6,-8.6]` — entirely off-screen. Either the vertex stage combines them differently than assumed, or the y translation is wrong for this target.
- Next probe: read the actual VS for pair 2/0 (`infernax-shaders-draft.h`) and check how `vs_c4_1`/`vs_c5_1`/`vs_c3` combine, instead of inferring from the matrices.

Probe of the client vertex buffer (`NVN_DRAW_VERTS`, glMapBufferRange) printed nothing — the draw does not come from a mapped GL buffer, so vertex positions must be read from guest memory at the attribute address instead.

### 0121: where the shader text actually lives

`glGetAttachedShaders(program)` returns nothing and `glGetShaderSource` prints 0 bytes — the game's
programs are assembled from pre-compiled shader *objects* held by id: `glAttachShader(pr,(GLuint)p->sources[0])`
(`nvn_render.h:1679`). So the GLSL text is not reachable through the linked program and must be read from
the source table those ids index (`p->sources[]`), which is also what produces the `vs_c4_1` style names
seen in `MOMO_UNIFORM_ENTRY`.

Next: locate that table, read the VS for the pair used by the failing draw, and check how it combines
`vs_c3` / `vs_c4_1` / `vs_c5_1` before changing anything about the constants.

## 0122–0124: constants are live, writes are allowed, fragments never arrive

- `MOMO_UNIFORM_PAIR=2/0 MOMO_UNIFORM_AFTER=20` shows the world matrix changing every draw
  (`640,2000,-98` → `3280,-360,-98` → `640,-2720,-98`), so the constants are not a stale snapshot.
- The VS is mathematically correct: it does `world` (`vs_c5_1`) then `proj` (`vs_c4_1`) in proper
  column-major order (`x*e0 + y*e4 + z*e8 + e12`), verified by expanding the generated GLSL.
- `proj` maps `y` through `-2/720` with `ty=-1`, i.e. on-screen only for `y∈[-720,0]`; the world matrix
  yields `y≈2000`, so the sprite is outside the clip volume unless the vertex data is in a different
  space than assumed. That is the remaining unknown.
- `NVN_DRAW_WRITE` for draws 0..3: `mask=1111`, `blend=0`, `src=ONE dst=ZERO` — fragments are allowed
  to write, so the red pre-clear surviving means no fragment was ever produced.
- Vertex source next: `nvn_render_state.h` can point attribute 0 at a client array (`buffer=0`), which
  GLES 3 does not accept; `NVN_DRAW_SRC` prints the array-buffer binding, pointer and, for the client
  path, the first four positions read straight from that memory.

## 0125–0127: the probe only ever looked at the centre 16×16

`nvn_texture_probe_max` (`nvn_bridge.h:146`) reads a single 16×16 block at the centre of the target,
so "max=0" never proved that a draw wrote nothing — only that the centre stayed untouched.

Widening the trace (`NX_DRAW_TRACE=400`) over one frame shows 70 of 399 draws *do* write pixels:
`number=1 target=0x3ba0073b8 max=253 px=0,132,128,255`, `number=6 target=0xc656990 max=188 px=0,0,188,255`,
and the teal refresh repeats every 6 draws. The scene target `0x3a5113af8` (532 draws) stays at centre-zero.

Next: dump whole targets to PPM (`NX_DUMP_FRAME`), because a centre probe cannot tell an empty scene from a
scene whose content sits away from the middle. The first attempt wrote nothing — the frame gate uses
`nvn_replay.last_frame`, which is the replay list's own counter and never reaches the requested frame in the
live stream; the gate has to read the runtime's per-frame counter instead.

## 0128–0129: the presented frame really is black

The on-demand capture works (`SIGUSR1` → `live-0/1/2.png`, 1280×720). All three are black: **0 non-black
pixels, max channel 0** — so the black screen is not an artefact of the centre-only probe; the image that
reaches the panel is empty. Meanwhile the frame does write pixels into `0x3ba0073b8` and `0xc656990`.

So the loss happens on the way to the presented surface, not in the draw stream alone.

Also new in this run: the guest died with `STOP status=NVN_GUEST_POINTER pc=main.text.seg+0x8356fc
detail=0x140000001c0` and `exit=24` instead of reaching frame 99. Not explained yet; the only difference
from the previous run is the `SIGUSR1` sent mid-run, so it may be capture-induced rather than a regression.

## 0130–0132: what actually gets presented

`NVN_PRESENT_SRC` names the presented surface: three 1920×1080 swapchain images
(`0xc656990`, `0xc656a10`, `0xc656a90`, names 1–3), rotated per frame, all centre-black at present time.
`0xc656990` is also the target that received `px=0,0,188,255` earlier — the composite does run, it just
covers almost nothing.

The screen pipeline is therefore: 532 draws into the 1280×720 scene target → composite into a 1920×1080
swapchain image → blit to fb. The presented images are black because the scene target they sample is black.

The whole-surface dump attempted inside present returns `status=0x0`: at that point the GL context is not
current (it is made current later in the dispatch), so every GL call there is a no-op. The dump must run
after `nvn_single_make_current`/the async reader wait, or go through `nvn_private_dump("frame-ppm", ...)`
which already runs with a live context.

## 0133: whole-texture probe — the scene is empty, the swapchain is not

`NX_PROBE_FULL=1` scans the entire target instead of a 16×16 centre block:

- `0x3a517bf98` 1280×720 — `max=0 nonblack=0` over all 921 600 pixels: the scene target is genuinely empty.
- `0xc656990` 1920×1080 — `max=188 nonblack=921600`: the swapchain is painted over exactly half its pixels
  (0,0,188 blue), i.e. the composite writes, but only every other row / half the area.
- `0x3a5113af8` 1024×32 — fully painted.

Two separate faults, both now measured:
1. nothing lands in the 1280×720 scene target (the transform/space problem);
2. the swapchain is only half-covered, and the captured/presented image is black although the texture
   holds blue — so the blit that produces the visible frame does not carry that content.

## 0134–0135: the only painted thing is a clear rectangle in the corner

`NX_PROBE_DUMP=1` writes whole targets to PPM:

- `0x3a518c228` 1280×720 — completely black (scene target, confirms 0133).
- `0x3a5115a58`/`0x3a5113af8` 1024×32 — a horizontal teal gradient, the port's own fullscreen quad.
- `0xc656990` 1920×1080 — one flat `0,0,188` rectangle covering **x 0…1280, y 360…1080**
  (exactly 1280×720, anchored bottom-left of a 1920×1080 surface). Nothing else is touched.

So the swapchain is not "half painted": it carries a single 1280×720 clear/quad in the corner of a
1920×1080 image. The guest's viewport is 1280×720 while the presented surface is 1920×1080 — a real
size mismatch — and the game's own scene drawing still produces nothing inside that region.

## 0136–0137: draw state is clean, and the tiles do cover the screen

`NX_DRAW_STATE=1` at the draw call:

```
n=1 prog=2 count=6 mask=1111 blend=0 depth=0 stencil=0 scissor_test=1 vp=0,0,1280,720 sc=0,0,16384,16384
```

Colour writes on, blending off, depth test off, stencil off, scissor wide, viewport exactly 1280×720.
Nothing in the GL state can swallow these fragments — so the geometry itself is outside clip space.

`MOMO_UNIFORM_PAIR=2/0` (existing dump) over consecutive draws of frame 1, `target=132`:

- draw 2: binding 2 = `4000 0 0 0 | 0 4000 0 0 | 0 0 1 0 | 640 2000 -98 1` — x centred (640), y=2000 above the top.
- draw 3: same scale, translation `3280 -360 -98` — right of the screen, y correct.
- binding 1 is the projection `2/1280, -2/720, -1, -1, 1`.

So the draws form a tile grid: individual tiles sit outside the view, but *some* tiles of a 532-draw frame must
land inside. That contradicts the fully black 1280×720 target from 0133/0135 and points at the target the dump
names (`id=132`) not being the texture the whole-surface probe read (`0x3a518c228`). That identity is the next
thing to pin down, before touching any transform.

## 0138: target identity is not the problem

```
NVN_DRAW_STATE target_id=132 target_obj=0x3a5115a58 1024x32 depth=0x0        (strip)
NVN_DRAW_STATE target_id=130 target_obj=0x3a517bf98 1280x720 depth=0x3a5129b58 (scene)
NVN_DRAW_STATE target_id=2   target_obj=0xc656990   1920x1080 depth=0x3a0fbe4c8 (present)
```

The scene draws do land in `0x3a517bf98` — exactly the texture whose whole-surface probe was `max=0 nonblack=0`.
So the probe and the draw agree; the target was never mismatched.

With state clean (0136) and the target correct, the remaining way to get zero fragments from a quad that is
partly on screen is a degenerate clip-space result, e.g. `w=0`. Notably binding 2 is **384 bytes with only the
first 128 non-zero** (`4000…`, `640 2000 -98 1`), the other 256 bytes are zeros — consistent with a constant
bank array where the shader indexes a bank dynamically, and our fixed-offset binding leaves it reading zeros.

Next measurement: disassemble program 2's vertex shader and check whether constant loads use an index register
(LDC with a computed offset) rather than a fixed offset.

## 0143: how the vertex shader actually reads its constants

`out143/shaders/nvn_shader_sources.h`, the `vs_c4_1`/`vs_c5_1` block:

```glsl
uniform highp sampler2D vs_c5_1;
...
float _90 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((9u >> 2) & 1023, 9u >> 12), 0)[9u & 3], ...);
gl_Position.w = fma(_108, texelFetch(vs_c4_1, ivec2(11u >> 2, 0), 0)[3], ...);
```

Constants are **not** `uniform` blocks: every element is fetched from a constant *texture*, index → texel
`(i/4, i/4096)`, component `i%4`. So `vs_c5_1` must contain the 96 elements of binding 2 as a 1024-wide texture,
element `i` at texel `(i/4, 0)`, channel `i%4`.

If that texture carries stale data or the wrong element order, `gl_Position.w` comes out 0 and every vertex is
clipped — clean state, correct target, zero fragments, no GL error. That is exactly what 0136–0143 measured.
Also note `NVN_UNIFORM_UPLOAD` never fired: the uniform upload code at `nvn_render.h:972` is not the path these
draws use — the constant-texture path (line ~922, `NX_CBUF_TEXTURE`) is.

Targets per frame, first visit: `133` 1024×32 (pair 14/1) paints to `max=253`; `130` 1280×720 (pair 2/0) stays
`max=0` for the whole pass; swapchain `id=2/3/4` reaches `max=188` only later.

## 0144–0145: the scene pass ends empty, and its four tiles frame a hole

`NX_TGT_FIRST` now probes the target it is *leaving*, so the number is the state at end of pass:

```
NVN_TARGET_END id=132 0x3a5113af8 1280x720  max=0     <- scene, after all 27 draws
NVN_TARGET_END id=134 0x3ba0073b8 1024x32   max=253
NVN_TARGET_END id=2   0xc656990   1920x1080 max=188
```

The scene pass really ends with zero pixels — not an artefact of probing too early.

`NX_TILE_TRACE` over the whole frame shows only four distinct world translations, cycling:

```
(640, 2000)  (3280, -360)  (640, -2720)  (-2000, -360)
```

Quad half-size 2000 (scale 4000, attr0 = ±0.5). Those four squares cover
x∈[-1360,2640]/y∈[0,4000], x∈[1280,5280]/y∈[-2360,1640], x∈[-1360,2640]/y∈[-4720,-720],
x∈[-4000,0]/y∈[-2360,1640] — i.e. they frame the view rectangle exactly and leave the interior uncovered:

```
        top tile
   left  [ HOLE ]  right
        bottom tile
```

A 2D tile layer cannot do that by accident. It is the signature of a missing rotation/camera stage: the tiles
are laid out around the view at 45°, with each tile's edges landing on the view edges. Constant `c5` carries a
pure scale+translation and the shader applies exactly one matrix, so the missing transform is not in the block we
bind — the next check is the original shader's constant usage against the generated GLSL.

## 0146: the generated VS is column-major-correct

`gl_Position.x = _108*c4[8] + _94*c4[0] + _90*c4[4] + c4[12]`, `w = _108*c4[11] + _94*c4[3] + _90*c4[7] + c4[15]`,
with `_90/_94/_108` built from `c5` rows — a standard column-major `P * (M * attr)`. No index bug in the generator.

But with the `c5` block we actually measured (`4000 0 0 0 | 0 4000 0 0 | 0 0 1 0 | 640 2000 -98 1`, elements
8..11 = `0 0 1 0`), the element the shader multiplies by `attr0.z` is `c5[8] = 0` and the translation lands in the
`c5[12..15]` slot the shader uses as the *fourth* column — so the arithmetic and the block content disagree.
Either the block we bind is not the one the shader indexes, or the constant texture is not what we uploaded.

Next measurement: read the constant *texture* back at draw time (`glGetTexImage` on `vs_c5_1`) instead of the source
buffer, and compare element 8 against the buffer.

## 0147–0150: found it — the scene draws run with no vertex buffer bound

Identity constants (`NX_CBUF_IDENTITY=1`) changed nothing: scene target still `max=0`, so constants are not the cause.
FBO is fine: `fbo=3 status=0x8cd5` (COMPLETE) for all 27 scene draws, `err=0x0`.

Vertex state at the draw (`NX_VB_CHECK`):

```
NVN_VB a=0 en=1 sz=3 ty=0x1406 stride=20 gsz=0 mok=0 ptr=0x3ac801000
NVN_VB a=1 en=1 sz=2 ty=0x1406 stride=20 gsz=0 mok=0 ptr=0x3ac80100c
idx=0,0,0,0,0,0
```

`gsz=0` — **no array buffer is bound** at draw time; the attribute pointers are raw guest addresses
(`0x3ac801000`). On GLES an attrib pointer with no bound VBO reads nothing, so every vertex arrives as zero and
every triangle is degenerate: clean state, complete FBO, valid constants, zero pixels. That is the black screen.

The 1024×32 strip renders because it goes through a different path that does bind a buffer.

## 0151–0154: geometry is real; client indices are not the cause

`GL_VERTEX_ARRAY_BUFFER_BINDING` (not the global array binding, which is legitimately 0 with no VAO): `vao=0 abuf=0`
for both attributes — the scene sets attributes from client pointers, as the design intends for desktop GL.

Guest memory at those pointers holds real data (not zeros):

```
ptr=0x3ac800800 f=-0.5,-0.5,-3.06e-17,0   0,-1,1,0
```

So vertices exist and the quad is ±0.5. Enabling `COTM_STREAM_VERTEX_BUFFERS=1` changed nothing (`max=0`), and
streaming indices through a real element buffer (`nvn_client_indices`) *lost* the present pass (`0xc656990` went
188 → 0) without gaining a pixel — reverted, device restored to 0153 (`d7e1e8d0…51203`).

Full state of the scene draw is now: complete FBO 3, viewport 0,0,1280,720, colour mask 1111, blend/depth/stencil
off, scissor 0,0,16384,16384, real vertices, real constants — and `max=0`.

The remaining arithmetic candidate: `gl_Position.w = z'*c4[11] + y'*c4[7] + x'*c4[3] + c4[15]` needs those four
elements to be `(0,0,0,1)`. Only the first five floats of `c4` were ever measured (`2/1280, -2/720, -1, -1, 1`).
If `c4[3]` is `-1` the quad's `w` collapses to `1 - x'` and the whole primitive is clipped — the exact symptom.
Next measurement: dump all 40 floats of binding 1 and compare against that requirement.

## 0155–0156: the scene pass never rasterizes — proven, not inferred

Added `MOMO_DIAG_FS=<index>|1`, which rewrites the fragment's final `}` to `frag_color0=vec4(1,0,0,1)` — it does not
depend on the alpha-test tail that `MOMO_DIAG_RED`/`MOMO_DIAG_NOCLIP` require (no generated fragment contains
`discard`, so those two hooks are inert in this port).

Run with `MOMO_DIAG_FS=1`: `MOMO_DIAG_FS_INSTALLED` ×3, strip `max=255`, swapchain `max=255`, scene target

```
NVN_TARGET_END id=132 obj=0x3a5113af8 1280x720 max=0
```

So it is not "the shader computes black": with a forced red fragment the scene still writes nothing. The triangle
never rasterizes. Combined with the earlier `NX_CBUF_IDENTITY` run (identity applied to `vs_c4 binding=1 n=65536`,
still `max=0`), the transform is not the only blocker — the draw itself is being discarded.

Next: inject a hardcoded full-screen quad into the vertex stage of this program (attribute-independent). Rasterizing
proves FBO+draw path are fine and narrows it to attribute/transform semantics; still empty means the draw call is
dropped before rasterization.

## 0157: retract the universal non-rasterization claim; measure the real transforms

The conclusion in 0155–0156 above was too strong. Consultations
`infernax-0157-cull-audit-35e72a8` and `infernax-0158-shader-frontier-f27dd3d9`
were completed through chatgpt-use. No fullscreen vertex substitution or matrix/cull override was made.

A bounded `NX_SCENE_AUDIT=1` measured pair 2/0 at frames 20 and 100, four rectangles at each frame.
The current/expected GL program matched, both stages compiled and the program linked, indices were
`0,1,2,2,1,3`, and immediate post-draw GL errors were zero. Full-target readbacks were black and
occlusion queries returned zero. CPU reproduction from the measured guest vertices and constants
places these eight rectangles outside the visible clip interior. That explains these draws without
proving a broken rasterizer. GPU constant-texture contents and other passes remain unqualified.
The historical text above is retained as an evidence trail, not an accepted diagnosis.

Run `arm-cg-157-contract-1793829993` reached frame 138, then encountered an unrecognized vertex shader:

- code: `f27dd3d93df99f91af7a594cee493a7d004ea2da86a3fd111db4c94500684f21` (1024 bytes);
- control: `4d2199467ee60ce35f0f392dff5c92aef82e2f12583e4f8ea0166f6e4a7fdb3a` (2176 bytes).

Live KMS was black; a fresh cleanup KMS capture showed the real Spruce menu. No game-picture success.
Build 0157, its arm config, and independent device read-back all agree on SHA-256
`bd714907a89fff3891e579e78b0d767c35fb06b9f7479bcea2f375177b0529ff`.
The different hash recorded in the 0158 consultation context was an attribution error, not the installed ELF.

## 0158: exact frontier translation, host verification complete; device arm pending

Both the original Hades compiler and the reflection build produced byte-identical GLSL and SPIR-V
from the verified captured pair. GLSL SHA-256:
`48e91692196e03b2811236c447403b05b38141b6ff0f5053ddf2b06d5b1d7f2a`;
SPIR-V: `44415320c20ef41aaca42748ebe7a71397b73ab21ae20ce53c494ee65da596a5`.
The owner blobs remain outside the repository.

Append one translated entry (index 15): preserve all 15 previous source/binding initializers byte-for-byte
and require code AND control hashes for the new entry. Its code hash does not collide with any legacy entry.
`INFERNAX_EXACT_SHADER` provides a one-shot identity receipt. Emit the primary STOP line before optional
thread/stack diagnostics, which could otherwise obscure the first fail-closed reason.

All 29 host tests pass against the exact native headers copied into out158. The test harness now declares
the replay draw counter referenced by the existing optional upload trace; production upload code is unchanged.
The initial suite invocation lacked `INFERNAX_NATIVE_HEADER`; a second exposed that stale fixture declaration.

Build 0158 completed with no compile errors and four existing ignored-write-result warnings:
AArch64 ELF, maximum required GLIBC 2.27, unchanged dependency list, SHA-256
`2511cb399842178f79a81c3915f3b5262323e00b0236be63fd51f22f1b0730e6`.
The render, shader and loader sources match the copied build inputs. No 0158 device result yet.
The next arm must keep memory/watchdog limits, raster state and old catalogue entries unchanged and
verify exact selection, the next named frontier, real live KMS pixels, and principal-owned cleanup.

## 0158–0160 device arms: startup RSS guards, then exact VS selected and next FS captured

Chatgpt-use consultations `infernax-0158-predevice-e4c3e0f`,
`infernax-0159-guard-repeat-e4c3e0f`, and `infernax-0160-attribution-e4c3e0f` completed.
The same out158 ELF was installed atomically, read back independently, and retained on the device.
Backup `infernax-nextos.bak-out157-before0158` matches the out157 SHA above.

- `arm-ch-158-exact-1793831577`: guard stopped frame 2 at RSS 755504 KiB versus cap 753664 KiB;
  MemAvailable 54660 KiB, SwapFree 1594796 KiB. The new shader was not encountered.
- `arm-ci-159-repeat-1793831964`: one unchanged repeat, guard stopped frame 2 at RSS 754152 KiB;
  MemAvailable 43736 KiB, SwapFree 1543448 KiB. Again no new shader selection. Both exits 137
  were controller TERM/KILL, not a demonstrated shader crash or kernel OOM.
- `arm-cj-160-smaps-1793832271`: same ELF, environment and thresholds; an external diagnostic
  controller could capture bounded proc memory details if the same guard fired. It did not fire:
  sampled RSS max 737872 KiB, MemAvailable min 50032 KiB. No smaps snapshot was produced,
  so pool attribution is still unknown. Guard and memory floor were never relaxed.

The third arm emitted exactly one `INFERNAX_EXACT_SHADER index=15 stage=0` with the expected
`f27dd3d9...` code and `4d219946...` control identities. The previous shader stop is resolved at
identity lookup. This is not by itself a GPU compile/link receipt. At present frame 122 it reached
a different missing shader, now with the primary `STOP status=ZERO_SHADER_NOT_PINNED` visible before
the secondary stack-census SI_TKILL fault. Frame numbers vary with startup timing and are not a
portable progress ordering; the changed shader identity is the frontier evidence.

The next pair is a fragment shader (SPH stage 5), captured and independently hashed:

- code 512 bytes: `b1a49d9c2f4b167cef4d73c3af8197a3ea2c5674c55dbd79fc3bc04ecdd9f8a4`;
- control 2176 bytes: `c7e6cd7d4710a47d407f7a2f4b644768152b463103078fd9156fa5cf43fd9ef6`.

Live KMS captures remain fully black. Each arm returned through principal to the real Spruce menu,
confirmed visually and by empty target/wrapper census and absent handoff. No game-picture success.

## 0161 candidate: append only the captured fragment stage

Both Hades compilers again produce byte-identical outputs:
GLSL `c77be13c5e1b16875b7c3f5d6897a45d4929ff72a5447e9a2adddcb0686848f2`;
SPIR-V `8edeb4d635cc7b3ece035924339a8abc75c3184a3a41d2da1901cb2e3da8bce0`.
The FS samples one texture at varying 2.xy and computes sampled RGBA * varying 0 + varying 1.
Actual reflection preserves independent texture c2 offset 0x168 and sampler c2 offset 0x568,
with no fallback or guessed second operand. Existing ESSL FMA-to-mul/add lowering and mediump policy
are unchanged, and are not claimed bit-exact to Maxwell arithmetic.

Append index 16 to form a 17-entry catalogue; all previous 16 source/binding initializers are
byte-identical and the strict identity boundary remains 15. No native render/loader change.
All 29 host tests pass. Device testing of this candidate is still pending.

## 0161–0162: full-readback diagnostics isolated; next exact shader frontier

Build 0161 completed with 0 errors / 4 existing warnings and passed all 29 tests against its copied
native headers. ELF SHA-256: `e92e1e538f83e112ee8d4b6d510cc486947eca02604ab214e2ab0c83b340d871`.
Atomic installation and independent readback passed; out158 was retained as a verified backup.
Consultations `infernax-0161-fragment-da00863` and `infernax-0162-probe-cost-da00863` completed.

Arm 0161 hit the unchanged RSS cap at frame 3 (754844 KiB, MemAvailable 50000 KiB), before VS15/FS16.
The proc snapshot exposes a major accounting caveat: 73 Mali PFNMAP/IO mappings report zero smaps RSS/PSS,
while status RssFile is 447376 KiB. The smaps rollup RSS of 289188 KiB cannot replace
RSS as a safety metric. No PSS-based weakening or driver pool/VM/clocks changes were made.
The asynchronous reader finished 1060 ms after guard because its alarm started only once scheduled;
parent TERM followed guard by 15 ms and did not wait. The next external controller fixes the remaining
alarm budget relative to the original guard timestamp, with an incomplete result if scheduling consumes it.

Arm `arm-cl-162-lightprobe-1793833270` changed only the diagnostic workload `NX_PROBE_FULL=1 -> 0`:
this removes repeated full-target readback/PPM allocations, not game pixels or normal present capture.
The same ELF and all guard thresholds remained. No full-probe/dump lines were emitted.
Sampled RSS max was 739844 KiB, MemAvailable min 42560 KiB; no resource guard.
Both exact receipts appeared (VS15 and FS16), and native pair 15/16 materialized as GL program 5
(`NVN_LC2_PROGRAM_REAL`), then a new shader identity stopped execution at present frame 210.
Five KMS live/late captures were still completely black. Real Spruce and exact process cleanup confirmed.
This one run does not prove a constant RSS saving or fix the black screen.

The next code/control pair is a 1792-byte vertex shader (SPH 1) plus 2176-byte control:
`ec0f3d6fc3c53e799e7e127b46c9d698e3f1de5cdfc2db2bc088bd53d803ae4c` /
`6e7d20fde156ddc62627824b245a917d5c4b86e2d6e6a3a4043c2702712371c8`.
Original and reflection Hades outputs are byte-identical: GLSL
`e4559b14bc7e8ba28ef16ef95cfeae6112c2f444324e5106cfc05f794752cb4e`, SPIR-V
`9e6deadd029e52cc1136e7a252be7f8deaafed8d79ded2381edc5b5d4047909b`.
Candidate 0163 appends index 17 only, preserving all previous 17 initializers and the strict boundary 15.
No native renderer change; device result remains pending.

## 0163 device result; 0164 exact fragment candidate

Consultation `infernax-0163-vertex-582e82d` completed. Build 0163 passed 29 tests and built with
0 errors / 4 existing warnings; SHA-256
`9cf9540ffec61f3886fc1e89e232271cea81bbb2b07a16bc7e33287aa734f838`.
Atomic install/fresh readback and backup out161 verified. The c1 literal is read from **code** offset
0x600 (word `0x509502f9`), inside the range [0x600,0x700) selected by control offsets 0x700/0x704;
the literal value itself is not in the control blob.

Arm `arm-cm-163-vertex-1793833660` retained FULL=0 and every safety limit. Sampled RSS max 746760 KiB,
MemAvailable min 50608 KiB, no resource guard. Exact index 17 appeared; the next missing shader is
another 512-byte fragment stage, reached at present frame 232. No new pair 17/18 link verdict yet.
Cleanup returned to Spruce with no target/wrappers/handoff. The image goal remains unmet.

Candidate 0164 appends index 18 only (catalogue 19); prior 18 initializers and strict boundary 15
are unchanged. Code/control:
`8572cb0aa8ced28417b5cbd36d8f2486acbe79b23fdc45ca2ca6160381d2a4ad` /
`b71eaefa3fc9a3d1d9f103aa43862ad3d2c8cbe29656154f657694dd383a2924`.
Both Hades compilers produce identical GLSL
`6142a5d067ce127c08d2306a436184ee25b21cfd07259de0406da8cb09ac88c2` and SPIR-V
`afc655279365fdce0733c3b0115b41dac2eeb8beaf39ad6240c5ff90fc61d659`.
Actual reflection carries two independent combined-at-GL operands: texture c2 0x168 / sampler c2 0x568,
and texture c2 0x170 / sampler c2 0x570. The FS uses varying 1.xy for sampled RGB and varying 3.xy
for sampled alpha, multiplied by varying 0; no guessed shared sampler. Native rendering code unchanged.

## 0164 device result: pair 17/18 links, first non-black presented frames

Build 0164 was installed atomically over out163 and read back as
`2f56db9d5f5f95b2686253feed5b21e13c491686533e8229d42469386d924b8a` (1986560 bytes);
the previous binary is retained as `infernax-nextos.bak-out163-before0164`
(`9cf9540ffec61f3886fc1e89e232271cea81bbb2b07a16bc7e33287aa734f838`).
Arm `arm-cn-164-fragment-1793834083` kept `NX_PROBE_FULL=0`, the 8 MiB runtime floor,
RSS cap 736 MiB, MemAvailable floor 16 MiB, wall 300 s / game 240 s; no other variable changed.

Both exact receipts appeared (`index=17` stage 0 `ec0f3d6f…ae4c`, `index=18` stage 1 `8572cb0a…a4ad`)
and the pair materialized as GL program 6:
`NVN_LC2_PROGRAM_REAL original-offline-pair=17/18 attributes=4 uniforms=3 program=6 code-control-source=byte-exact`.
A catalogue lookup receipt is not a link receipt; this line is the link/pair receipt for the first time
on this frontier.

The present probe changed state for the first time in this port. Frames 1–238 read
`status=BLACK-SAMPLE nonblack=0`; frame 239 is the first non-black sample (`nonblack=2208 max=32`),
frame 240 reads `nonblack=44181 max=63`, and frames 252–337 are stable at
`nonblack=5844 max=254 white=3209 texture-draws=113 status=PIXELS`. The draw probe (gated to the first
draws) recorded draw #7 on `0x3a5115a58` as `max=253 px=0,132,131,255`.

The guard fired first. At present frame 237 the sample read VmRSS 754092 KiB against the 736 MiB cap
(753664 KiB) — 428 KiB over — with RssFile 509392, RssAnon 243412, RssShmem 1288, VmSwap 458736,
MemAvailable 60728 KiB and SwapFree 1541944 KiB. The attribution reader completed in 157 ms.
The controller then sent TERM (mono 39356.99) and SIGKILL (mono 39364.11), so every frame from 239 to 337
was produced inside that 5 s window (~14 fps): the log cursor at guard time had only reached frame 237.
The first non-black sequence is therefore real GL pixels of this title but **post-TERM**; an uninterrupted
run has not yet shown content, and this arm does not prove the panel can show a picture.

The three live KMS captures were taken on the fixed 0/8/16 s schedule from capture-ready, which landed at
present frames ~1–30, i.e. before frame 239; all three decode to a single colour (black) and say nothing
about the later frames. `cleanup-1.png` shows the Spruce menu (3106 distinct colours); no target, wrapper
or `/tmp/cmd_to_run.sh` remained. `exit=137` is our own SIGKILL, not a guest fault.

Image goal still unmet. Next decision is a memory-guard/capture-window question, not a shader one.

## 0165 device result: real panel picture, then the nvdec frontier

Two harness-only runs (`run_controller165.py`, SHA-256
`6947dead4e91a9ee46427e1ae82d43995d15be74fdf50bf06faec8d14c8922f9`) used the installed 0164 ELF
`2f56db9d…24b8a`, the same env, the same RSS/MemAvailable floors and the same principal flow. The only
change was the physical KMS capture trigger; the fixed 0/8/16 s schedule was removed.

Run `arm-co-165-pixels-1793834300` fired the trigger on present frame 1. Every run of this title starts
with a full-surface non-black initial buffer (`nonblack=921600 max=188`) that is not content, so both
captures were black and this arm measured a trigger defect, not a renderer result. The guard also fired
at 756052 KiB before index 17/18 appeared.

Run `arm-co2-165-transition-1793834600` triggered on the first non-black present that follows a black one:

- first content present at frame 222 (`nonblack=2208 max=32`), then 603 non-black presents over frames
  222–828 with `texture-draws` rising 74 → 276;
- no resource guard: RSS peak 753528 KiB against the 753664 KiB cap;
- both exact receipts and `NVN_LC2_PROGRAM_REAL original-offline-pair=17/18 … program=6 code-control-source=byte-exact`;
- two KMS captures taken while the owned process was alive, at the transition and 0.3 s later
  (`pixels-0.png` `74c019961dc6272e7035da316f0ff5b781d368d6934b294709c3255ea693f1f8`,
  `pixels-1.png` `cd8022756521dcb2c5da7d13734251898855f0036ad55b7d207d48328cf2830b`).
  Both decode to 1280x720 with 149 / 214 distinct colours: black background, a grey/white logo band
  spanning y=172..508 and a red element, with the brightness ramping 159 → 254 between the two frames.
  The GL → EGL/KMS → panel path is therefore proven in a normal, non-TERM run for the first time.

The title then stops itself at present frame 838:
`STOP status=UNIMPLEMENTED_NV_DEVICE pc=sdk.text.seg+0x158b20 detail=0x5`, `exit=20`, immediately after
`NV_OPEN path=/dev/nvhost-ctrl` and `NV_OPEN path=/dev/nvhost-nvdec` with `SVC id=0x21 handle=0x411`.
The guest asks for the hardware video decoder. That is the new frontier; the picture itself is no longer
the blocker. Cleanup verified: no target, wrapper or handoff, MainUI back, Spruce menu on the panel.

Gameplay, input and audio remain unqualified. KMS captures stay local (owner content).
