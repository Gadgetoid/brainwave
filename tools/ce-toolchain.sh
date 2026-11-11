#!/bin/sh
# Fetches the pinned grimace and LLVM for Windows CE into DIRECTORY.
set -eu

directory=${1:?Usage: tools/ce-toolchain.sh DIRECTORY}
grimace_repository=https://github.com/grimace-sdk/grimace.git
grimace_commit=5120250dd63d8e5c8c1bae3335998ead9d1fc810
llvm_url=https://github.com/grimace-sdk/llvm-builds/releases/download/v3/velo-llvm-v3-linux-x86_64.tar.xz
llvm_sha256=c3f0b67e22105ef68bbb71d0a316b268ef3e44e5b83aab93b0f9a071d2706b77

mkdir -p "$directory"
if [ ! -d "$directory/grimace/.git" ]; then
    git init -q "$directory/grimace"
    git -C "$directory/grimace" remote add origin "$grimace_repository"
fi
git -C "$directory/grimace" fetch -q --depth 1 origin "$grimace_commit"
git -C "$directory/grimace" checkout -q --detach FETCH_HEAD

if [ ! -x "$directory/llvm/bin/clang" ]; then
    curl -sSfL -o "$directory/llvm.tar.xz" "$llvm_url"
    echo "$llvm_sha256  $directory/llvm.tar.xz" | sha256sum -c --quiet
    mkdir -p "$directory/llvm"
    tar -xJf "$directory/llvm.tar.xz" -C "$directory/llvm" --strip-components=1
fi
echo "grimace $grimace_commit"
echo "llvm $llvm_url $llvm_sha256"
