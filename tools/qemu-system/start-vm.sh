#!/bin/sh
# Full-system ARM64/TCG VM; all network listeners stay on AOOSTAR loopback.
set -eu
root=$(realpath "${1:?usage: start-vm.sh WORKSPACE}")
test -f "$root/vm.qcow2"
test -f "$root/seed.iso"
test -f "$root/uefi-vars.fd"
test ! -e "$root/qmp.sock"
podman run --detach --name infernax-qemu-system-0170 \
    --security-opt label=disable --network host --group-add keep-groups \
    --device /dev/dri/renderD128 --cpus 6 --memory 12g \
    -v "$root:/work" localhost/infernax-qemu-system:bookworm \
    qemu-system-aarch64 -name infernax-arm64-0170 \
    -machine virt,accel=tcg -cpu cortex-a53 -smp 8 -m 8192 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd \
    -drive if=pflash,format=raw,file=/work/uefi-vars.fd \
    -drive if=virtio,format=qcow2,file=/work/vm.qcow2 \
    -drive if=virtio,format=raw,readonly=on,file=/work/seed.iso \
    -device virtio-gpu-gl-pci,xres=1280,yres=720 \
    -display egl-headless,rendernode=/dev/dri/renderD128 \
    -vnc 127.0.0.1:70 \
    -device virtio-keyboard-pci -device virtio-tablet-pci -device virtio-rng-pci \
    -audiodev none,id=audio0 -device ich9-intel-hda -device hda-duplex,audiodev=audio0 \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:22270-:22 \
    -device virtio-net-pci,netdev=net0,romfile= \
    -virtfs local,path=/work/share,mount_tag=hostshare,security_model=mapped-xattr \
    -serial file:/work/serial.log -monitor none \
    -qmp unix:/work/qmp.sock,server=on,wait=off
