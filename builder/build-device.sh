#!/bin/bash

project_path=$(cd "$(dirname "$0")/.." && pwd)
build_path=$(cd "$project_path/.." && pwd)

cd "$project_path"

. shared.sh

export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

EXTERNAL_PATH="$project_path/base_external"

git submodule init
git submodule sync
git submodule update

set -e

if [ ! -e "$build_path/buildroot/.config" ]
then
    echo "MISSING BUILDROOT CONFIGURATION FILE"

    if [ -e "$AESD_MODIFIED_DEFCONFIG" ]
    then
        echo "USING $AESD_MODIFIED_DEFCONFIG"
        make -C "$build_path/buildroot" defconfig BR2_EXTERNAL="$EXTERNAL_PATH" BR2_DEFCONFIG="$AESD_MODIFIED_DEFCONFIG"
    else
        echo "Run ./save_config.sh to save this as the default configuration in $AESD_MODIFIED_DEFCONFIG"
        echo "Then add packages as needed to complete the installation, re-running ./save_config.sh as needed"
        make -C "$build_path/buildroot" defconfig BR2_EXTERNAL="$EXTERNAL_PATH" BR2_DEFCONFIG="$AESD_DEFAULT_DEFCONFIG"
    fi
else
    echo "USING EXISTING BUILDROOT CONFIG"
    echo "To force update, delete .config or make changes using make menuconfig and build again."
    make -C "$build_path/buildroot" BR2_EXTERNAL="$EXTERNAL_PATH"
fi
