# First NVN program (no catalogue entry)

Measured on device 2026-11-04, run `COTM_RUN_ID=1 NX_SHADER_DUMP=1`.

```
ZERO_SHADER_UNRECOGNIZED code-bytes=256 control-bytes=2176
  code-hash=b3913c5cf5773c0e3a8d943f162b9d6d94297f269402690306575186a56e88ff
 control-hash=c1e1e8ffdd8458ccec235f6913dff771d84e0bd80ce52ab6ea5f0e75889f59ba
STOP status=ZERO_SHADER_NOT_PINNED pc=main.text.seg+0x876950
```

Raw bytes are in `evidence/shader-01/` (`nvn-program-code-1-0.bin` 256 B,
`nvn-program-control-1-0.bin` 2176 B).

- code begins `78 56 34 12` (0x12345678), control begins `34 12 76 98` (0x98761234):
  both are NVN container blobs, not bare instructions.
- 256 B total with a 0x30-byte header leaves ~0xD0 B of body — a very small program
  (a blit/UI-ish shader), which is the cheapest possible first catalogue entry.
- The local envytools build has no NVIDIA architectures compiled in
  (`envydis -l` lists none), so disassembly needs a rebuild with GPU support before
  the body can be identified as vs/fs/gs and translated.
