#!/bin/bash

project_path=$(cd "$(dirname "$0")/.." && pwd)
build_path=$(cd "$project_path/.." && pwd)

cd "$project_path"

. shared.sh

mkdir -p base_external/configs/

make -C "$build_path/buildroot" savedefconfig BR2_DEFCONFIG="$project_path/$GATEWAY_MODIFIED_DEFCONFIG"

if [ -e "$build_path/buildroot/.config" ] && ls "$build_path"/buildroot/output/build/linux-*/.config 1> /dev/null 2>&1; then
    if grep "BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE" "$build_path/buildroot/.config" > /dev/null; then
        echo "Saving linux defconfig"
        make -C "$build_path/buildroot" linux-update-defconfig
    fi
fi
