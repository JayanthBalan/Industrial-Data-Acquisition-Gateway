#!/bin/bash

run_path=$(cd "$(dirname "$0")/.." && pwd)

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 -nographic -smp 1 \
    -kernel ${run_path}/buildroot/output/images/Image \
    -append "rootwait root=/dev/vda console=ttyAMA0" \
    -netdev user,id=eth0,hostfwd=tcp::10022-:22,hostfwd=tcp::9000-:9000,hostfwd=tcp::9196-:9196 \
    -device virtio-net-device,netdev=eth0 \
    -drive file=${run_path}/buildroot/output/images/rootfs.ext2,if=none,format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0 -device virtio-rng-pci