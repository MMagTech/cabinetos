#!/usr/bin/env bash
# THROWAWAY MEASUREMENT, on a probe branch that is never merged.
set -uo pipefail
raw=$1; work=$2
say() { printf '\n##### %s\n' "$*"; }
mkdir -p "$work"; cd "$work"

say "the built file"; ls -ls "$raw"; sudo sfdisk -d "$raw"
LOOP=$(sudo losetup -fP --show "$raw"); echo "loop $LOOP"; sleep 2
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTTYPENAME "$LOOP"
P=$(lsblk -ln -o NAME,FSTYPE "$LOOP" | awk '$2=="btrfs"{print $1}'); P=/dev/$P; echo "root part $P"
ESP=/dev/$(lsblk -ln -o NAME,FSTYPE "$LOOP" | awk '$2=="vfat"{print $1}')
BOOT=/dev/$(lsblk -ln -o NAME,FSTYPE "$LOOP" | awk '$2=="ext4"||$2=="xfs"{print $1}'); echo "boot $BOOT esp $ESP"
mkdir -p esp boot root
sudo mount -o ro "$ESP" esp && { say ESP; sudo find esp -type f -printf '%10s %p\n'; sudo sh -c 'for f in esp/EFI/fedora/*.CSV; do echo "== $f"; iconv -f UTF-16 "$f"; done'; sudo cat esp/EFI/fedora/grub.cfg 2>/dev/null; sudo cat esp/EFI/BOOT/grub.cfg 2>/dev/null; sudo df -h esp; sudo umount esp; }
if [[ -b "$BOOT" ]]; then sudo mount -o ro "$BOOT" boot && { say BOOT; sudo find boot -maxdepth 3 | head -40; sudo sh -c 'for f in boot/loader*/entries/*; do echo "== $f"; cat "$f"; done'; sudo sed -n 1,60p boot/grub2/grub.cfg; sudo cat boot/grub2/grubenv 2>/dev/null; sudo df -h boot; sudo umount boot; }; fi

say "root before"
sudo mount -o compress=zstd:1 "$P" root; mount | grep "$P"
sudo btrfs filesystem usage root; sudo btrfs subvolume list root; sudo btrfs inspect-internal min-dev-size root
sudo du -sh --apparent-size root 2>/dev/null | tail -1; sudo du -sh root 2>/dev/null | tail -1
sudo ls -la root root/root root/root/ostree 2>/dev/null | head -30
sudo grep -E '^(root|cabinet)' root/root/ostree/deploy/default/deploy/*.0/etc/passwd 2>/dev/null
sudo sh -c 'cut -d: -f1,2 root/root/ostree/deploy/default/deploy/*.0/etc/shadow | cut -c1-40' 2>/dev/null | head -5
sudo sh -c 'cat root/root/ostree/deploy/default/deploy/*.origin' 2>/dev/null
sudo sh -c 'ls -la root/root/ostree/deploy/default/deploy/*.0/etc/machine-id; wc -c root/root/ostree/deploy/default/deploy/*.0/etc/machine-id' 2>/dev/null

say "shrink"
time sudo btrfs balance start -dusage=90 -musage=90 root || true
sudo btrfs filesystem usage -b root | sed -n 1,14p
min=$(sudo btrfs inspect-internal min-dev-size root | awk '{print $1}'); echo "min $min"
target=$(( (min / 1048576 + 1024) * 1048576 )); echo "target $target"
time sudo btrfs filesystem resize "$target" root
sudo btrfs filesystem usage root | sed -n 1,14p
time sudo fstrim -v root
sudo umount root
sudo btrfs check --readonly "$P" 2>&1 | tail -8
sudo losetup -d "$LOOP"

say "file shrink"
sect=$(( target / 512 ))
n=$(sudo sfdisk -d "$raw" | grep -n "^$raw" | tail -1 | sed -E "s|^[0-9]+:$raw([0-9]+).*|\1|"); echo "last part $n"
start=$(sudo sfdisk -d "$raw" | grep "^$raw$n" | sed -E 's/.*start= *([0-9]+).*/\1/'); echo "start $start"
echo ", ${sect}" | sudo sfdisk --quiet --no-reread --no-tell-kernel -N "$n" "$raw"
end=$(( start + sect )); tail=$(( end + 34 + 2048 ))
truncate -s $(( (tail / 2048 + 1) * 1048576 )) "$raw"
sudo sgdisk -e "$raw"; sudo sgdisk -v "$raw"; sudo sfdisk -d "$raw"; ls -ls "$raw"; sha256sum "$raw"

say "kernel arguments, as an ISO install's"
LOOP=$(sudo losetup -fP --show "$raw"); sleep 2
BOOT=/dev/$(lsblk -ln -o NAME,FSTYPE "$LOOP" | awk '$2=="ext4"||$2=="xfs"{print $1}')
sudo mount "$BOOT" boot
sudo sh -c 'sed -i -E "/^options /{s/ console=[^ ]+//g; s/^options /options rhgb quiet /}" boot/loader*/entries/*.conf; cat boot/loader*/entries/*.conf'
sudo umount boot; sudo losetup -d "$LOOP"

say "compression"
for lvl in 6 9; do /usr/bin/time -v xz -T0 -$lvl -k -c "$raw" > "x$lvl.xz" 2> "x$lvl.log"; ls -l "x$lvl.xz"; grep -E 'Elapsed|Maximum resident' "x$lvl.log"; done
xz --list --verbose x9.xz | head -14; /usr/bin/time -v xz -dc x9.xz 2> dx.log | sha256sum; grep -E 'Elapsed|Maximum resident' dx.log
rm -f x6.xz

say "boot"
ls -l /dev/kvm && sudo chmod 666 /dev/kvm
cp --sparse=always "$raw" test.img; truncate -s 64G test.img; ls -ls test.img
cp /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd; cp vars.fd vars.before
unit='[Unit]
Description=CI: the journal on the serial port
DefaultDependencies=no
After=systemd-journald.socket
[Service]
ExecStart=/bin/sh -c "exec journalctl --follow --no-pager --all --no-hostname -o short-monotonic >/dev/ttyS0 2>&1"
'
dropin='[Unit]
Wants=ci-journal.service
'
accel=kvm; cpu=host; [[ -w /dev/kvm ]] || { accel=tcg; cpu=max; }
qemu-system-x86_64 -machine q35,accel=$accel -cpu $cpu -smp "$(nproc)" -m 8192 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=vars.fd \
  -device virtio-vga,xres=1280,yres=800 -display none \
  -device qemu-xhci -device usb-kbd -device usb-tablet -nic none \
  -drive file=test.img,format=raw,if=none,id=usb0 -device usb-storage,drive=usb0,removable=on,bootindex=1 \
  -smbios "type=11,value=io.systemd.credential.binary:systemd.extra-unit.ci-journal.service=$(base64 -w0 <<<"$unit")" \
  -smbios "type=11,value=io.systemd.credential.binary:systemd.unit-dropin.sysinit.target~ci=$(base64 -w0 <<<"$dropin")" \
  -serial file:serial.log -qmp unix:qmp.sock,server,wait=off &
qpid=$!
start=$SECONDS; res=timeout
qmp() { python3 - "$@" <<'PY'
import json, socket, sys
s = socket.socket(socket.AF_UNIX); s.connect("qmp.sock"); f = s.makefile("rw"); f.readline()
def call(c, **a):
    f.write(json.dumps({"execute": c, "arguments": a}) + "\n"); f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r: return r
call("qmp_capabilities")
if sys.argv[1] == "shot": print(call("screendump", filename=sys.argv[2], format="png"))
if sys.argv[1] == "off": print(call("system_powerdown"))
PY
}
n=0
while (( SECONDS - start < 2100 )); do
  sleep 30; n=$((n+1))
  kill -0 $qpid 2>/dev/null || { res=vm-exited; break; }
  ((n % 4 == 0)) && { qmp shot "$PWD/shot-$n.png" >/dev/null; printf '%4ds serial %s bytes, resets so far %s\n' $((SECONDS-start)) "$(stat -c %s serial.log)" "$(grep -c 'Linux version' serial.log)"; }
  if grep -aq 'first-run. starting at' serial.log; then res=reached; echo "reached at $((SECONDS-start))s"; sleep 45; qmp shot "$PWD/first-run.png"; break; fi
done
echo "RESULT: $res after $((SECONDS-start))s"
say "serial, grep"; grep -aE 'first-run\]|cabinetos-grow-root|hardware-setup|Reached target|cabinetos-update-source|cabinetos-session|is up|Failed' serial.log | cut -c1-200 | tail -70
say "serial, head and tail"; head -c 3000 serial.log; echo ...; tail -n 40 serial.log | cut -c1-200
qmp off; for i in $(seq 1 40); do kill -0 $qpid 2>/dev/null || break; sleep 3; done; kill $qpid 2>/dev/null; sleep 2

say "after: firmware variables"; cmp vars.before vars.fd | head -2; strings -el vars.fd | grep -iE '^(fedora|cabinet|Boot[0-9]|.*shim|.*\.efi)' | sort -u | head -20; strings -el vars.before | grep -iE '^(fedora|cabinet)' | sort -u | head
say "after: the drive"
LOOP=$(sudo losetup -fPr --show test.img); sleep 2; lsblk -o NAME,SIZE,FSTYPE "$LOOP"
P=/dev/$(lsblk -ln -o NAME,FSTYPE "$LOOP" | awk '$2=="btrfs"{print $1}')
sudo mount -o ro,norecovery "$P" root 2>/dev/null || sudo mount -o ro "$P" root
sudo btrfs filesystem usage root | sed -n 1,10p
D=$(sudo sh -c 'ls -d root/root/ostree/deploy/default/deploy/*.0 | head -1'); sudo sh -c "cut -d: -f1,2 $D/etc/shadow | cut -c1-12 | grep -vE ':!|:\*' ; grep -E '^(cabinet|root)' $D/etc/shadow | cut -c1-16"
sudo sh -c 'ls root/root/ostree/deploy/default/deploy/; cat root/root/ostree/deploy/default/deploy/*.origin | head -8'
J=root/root/ostree/deploy/default/var/log/journal; sudo ls -la "$J"/* 2>/dev/null | head
sudo journalctl --directory="$(sudo sh -c "ls -d $J/*")" --no-pager -o short-monotonic 2>/dev/null | grep -aE 'first-run\]|cabinetos-grow-root|cabinetos-update-source|is up|bazzite-hardware-setup' | cut -c1-200 | tail -40
sudo umount root; sudo losetup -d "$LOOP"
exit 0
