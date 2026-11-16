#!/bin/sh
# Fetches the pinned grimace and LLVM for Windows CE into DIRECTORY.
set -eu

directory=${1:?Usage: tools/ce-toolchain.sh DIRECTORY}
grimace_repository=https://github.com/grimace-sdk/grimace.git
grimace_commit=d28782e6588e640b9c32eb8f91fe56d97609c10e
llvm_url=https://github.com/grimace-sdk/llvm-builds/releases/download/v6/llvm-v6-linux-x86_64.tar.xz
llvm_sha256=7a694afcc9a2b5d16a0f93fd5b13e7dba3e7ff15f0b2d31118356fa51cfc9254

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
