#!/bin/bash

set -e
set -o xtrace

apt-get update
apt-get install -y --no-remove \
        zstd \
        g++-mingw-w64-i686 \
        g++-mingw-w64-x86-64

mkdir ~/tmp
pushd ~/tmp
MINGW_PACKET_LIST="
mingw-w64-x86_64-headers-git-10.0.0.r14.ga08c638f8-1-any.pkg.tar.zst
mingw-w64-x86_64-vulkan-loader-1.3.211-1-any.pkg.tar.zst
mingw-w64-x86_64-libelf-0.8.13-6-any.pkg.tar.zst
mingw-w64-x86_64-zlib-1.2.12-1-any.pkg.tar.zst
mingw-w64-x86_64-zstd-1.5.2-2-any.pkg.tar.zst
"

for i in $MINGW_PACKET_LIST
do
  wget -q --tries=3 https://mirror.msys2.org/mingw/mingw64/$i
  tar xf $i --strip-components=1 -C /usr/x86_64-w64-mingw32/
done
popd
rm -rf ~/tmp

. .gitlab-ci/container/debian/x86_build-mingw-patch.sh
. .gitlab-ci/container/debian/x86_build-mingw-llvm.sh
