#!/usr/bin/env python3
"""Create a private NoCloud seed and qcow2 overlay; never change the base image."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
base = root / 'images/debian-12-generic-arm64.qcow2'
receipt = json.loads((root / 'images/verified.json').read_text())
assert receipt['name'] == base.name and receipt['bytes'] == base.stat().st_size
with base.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha512').hexdigest() == receipt['sha512']
key = (root / 'share/baseline/id_ed25519.pub').read_text().strip()
assert len(key.splitlines()) == 1 and key.startswith('ssh-ed25519 ') and len(key.split()) >= 2
seed = root / 'seed'
seed.mkdir(mode=0o700, exist_ok=False)
os.chmod(root, 0o700)
userdata = '''#cloud-config
hostname: infernax-arm64
manage_etc_hosts: true
disable_root: false
ssh_pwauth: false
users:
  - name: root
    lock_passwd: true
    ssh_authorized_keys:
      - KEY
package_update: true
packages:
  - rsync
  - python3
  - xserver-xorg-core
  - xserver-xorg-video-fbdev
  - xserver-xorg-input-libinput
  - xinit
  - openbox
  - x11-utils
  - x11-xserver-utils
  - xdotool
  - imagemagick
  - mesa-utils
  - mesa-utils-extra
  - libegl1
  - libgles2
  - libgl1-mesa-dri
  - libsdl2-2.0-0
  - alsa-utils
write_files:
  - path: /etc/ssh/sshd_config.d/90-local-vm.conf
    permissions: '0644'
    content: |
      PermitRootLogin prohibit-password
      PasswordAuthentication no
  - path: /etc/modules-load.d/infernax-test.conf
    content: |
      virtio_gpu
      uinput
mounts:
  - [hostshare, /mnt/host, 9p, 'trans=virtio,version=9p2000.L,msize=1048576,cache=none,nofail', '0', '0']
runcmd:
  - [modprobe, uinput]
  - [modprobe, virtio_gpu]
  - [mkdir, -p, /mnt/host, /opt/infernax-runs]
  - [sh, -c, 'mountpoint -q /mnt/host || mount /mnt/host']
  - [sh, -c, 'uname -a > /mnt/host/guest-ready.txt; dpkg-query -W >> /mnt/host/guest-ready.txt']
final_message: 'Infernax ARM64 VM provisioned after $UPTIME seconds'
'''.replace('KEY', key)
(seed / 'user-data').write_text(userdata)
(seed / 'meta-data').write_text('instance-id: infernax-arm64-0170\nlocal-hostname: infernax-arm64\n')
image = 'localhost/infernax-qemu-system:bookworm'

def container(*args):
    subprocess.run(['podman', 'run', '--rm', '--security-opt', 'label=disable',
                    '-v', str(root) + ':/work', image, *args], check=True)

assert not (root / 'vm.qcow2').exists(), 'Refusing to replace an existing VM disk'
container('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b',
          '/work/images/' + base.name, '/work/vm.qcow2', '24G')
container('genisoimage', '-output', '/work/seed.iso', '-volid', 'cidata',
          '-joliet', '-rock', '/work/seed/user-data', '/work/seed/meta-data')
container('cp', '/usr/share/AAVMF/AAVMF_VARS.fd', '/work/uefi-vars.fd')
print(json.dumps({'root': str(root), 'base': str(base), 'seed': str(seed),
                  'image': image, 'disk': 'vm.qcow2'}, indent=2))
