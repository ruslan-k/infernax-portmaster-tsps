# QEMU System ARM64 test bench

This is a full-system **ARM64/TCG** VM, not qemu-user. The first bench is on
AOOSTAR, isolated from the host desktop and the TSPS. It uses Debian 12's generic
ARM64 cloud image, an immutable verified base plus a writable qcow2 overlay,
Cortex-A53 x8, 8 GiB guest RAM, virtio/virgl graphics and a 1280x720 virtual display.
The rootless container is capped at 6 host CPUs and 12 GiB. Host listeners bind
only to loopback (SSH 22270, VNC 5970); QMP uses a private Unix socket.

The user-directed order is: reproduce baseline, reach a visually verified game
menu in the VM, then deploy and test on TSPS through Spruce. **No VM result
qualifies Mali, physical controls, audible output, handheld memory or speed.**

## Reproduce this bench

Use a private workspace on a writable filesystem; no system packages are needed.
Build `Containerfile` with rootless Podman as `localhost/infernax-qemu-system:bookworm`.
Download `debian-12-generic-arm64.qcow2` and `SHA512SUMS` from
<https://cloud.debian.org/images/cloud/bookworm/latest/> into `images/`, verify the
matching SHA-512, and record `name`, `bytes`, `sha512` in `images/verified.json`.
The initial verified image was 437649408 bytes; the exact digest is in the local
receipt, not assumed to identify a future `latest` download.

Put the owner's **private** image segments, RomFS and pinned runtime in
`share/baseline/`, along with the operator's `id_ed25519.pub`. Never copy a private
key. The original TSPS runtime is pinned to SHA-256
`258a53591bdb77d7a41a92be2a7453fb4e4b8ee1282754a5aa5607fa383cbe86`.
Keep owner game files, generated VM disks, SSH host keys and screenshots out of git.
The VM receives a separate work directory; never link original saves for writing.

```sh
python3 prepare-vm.py /absolute/private/workspace
sh start-vm.sh /absolute/private/workspace
podman inspect infernax-qemu-system-0170
podman logs infernax-qemu-system-0170
```

`prepare-vm.py` rechecks the base SHA-512, validates the public key, and refuses an
existing seed or VM disk. Initial cloud-init installs SDL2, Mesa, Xorg, Openbox,
Python, capture/input tools and mounts the private `share/` using virtio-9p.
Require the actual `share/guest-ready.txt` receipt and successful SSH before a game
run. A container ID is not a successful boot. Failed containers are retained for
logs; inspect the exact stopped container before removing it or its stale socket.
The virtio-net option ROM is disabled because boot is from disk, not PXE.

## Evidence and limits

Follow `ruslan-k/PM-Porting-Tools` at
`f41854f08121823020e603325f01ddfff8509048`, specifically
`docs/testing-and-debugging.md` and `devtools/native-testing/`. Those scripts use
qemu-user/Xvfb; they do not themselves provide this full VM. Reuse their real
frame comparison and guest uinput pad rather than emulating SDL success. Keep
normal clocks for loading/watchdog checks. Prove capture/diff sensitivity using
identical and changed fixtures before trusting a game screenshot.

No patched runtime has been deployed by creating this bench. Software OMX
selection remains a separate reversible experiment. Do not use fake nvdec fds,
no-op ioctls, removed movies, reduced-resolution assets or capability overrides.
Retain the real ARM64 runtime, full-size content, logs, input receipts and actual
rendered frames. Compare baseline and candidate on this bench before TSPS.

When finished, stop only this VM, preferably through its guest shutdown or QMP
`system_powerdown`; use the exact owned container name for a necessary stop.
Never kill host QEMU/Xorg/game processes by a broad name pattern.
