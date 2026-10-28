#!/bin/sh
# Fetches the pinned jornada-72x-tools and LLVM for Windows CE into DIRECTORY.
set -eu

directory=${1:?Usage: tools/ce-toolchain.sh DIRECTORY}
jornada_repository=https://github.com/Gadgetoid/jornada-72x-tools.git
jornada_commit=71f74221832c0551f8df257491f5b94a771d4057
llvm_url=https://github.com/Gadgetoid/velo-llvm-builds/releases/download/v3/velo-llvm-v3-linux-x86_64.tar.xz
llvm_sha256=c3f0b67e22105ef68bbb71d0a316b268ef3e44e5b83aab93b0f9a071d2706b77

mkdir -p "$directory"
if [ ! -d "$directory/jornada-72x-tools/.git" ]; then
    git init -q "$directory/jornada-72x-tools"
    git -C "$directory/jornada-72x-tools" remote add origin "$jornada_repository"
fi
git -C "$directory/jornada-72x-tools" fetch -q --depth 1 origin "$jornada_commit"
git -C "$directory/jornada-72x-tools" checkout -q --detach FETCH_HEAD

if [ ! -x "$directory/llvm/bin/clang" ]; then
    curl -sSfL -o "$directory/llvm.tar.xz" "$llvm_url"
    echo "$llvm_sha256  $directory/llvm.tar.xz" | sha256sum -c --quiet
    mkdir -p "$directory/llvm"
    tar -xJf "$directory/llvm.tar.xz" -C "$directory/llvm" --strip-components=1
fi
echo "jornada-72x-tools $jornada_commit"
echo "llvm $llvm_url $llvm_sha256"
