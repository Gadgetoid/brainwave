#!/bin/sh
# Packages a build into brainwave-VERSION.zip: the card folder, the first-flash UF2s, docs and checksums.
set -eu

repository=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:?Usage: tools/package.sh BUILD_DIRECTORY VERSION}" && pwd)
version=${2:?Usage: tools/package.sh BUILD_DIRECTORY VERSION}
name=brainwave-$version
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
package=$stage/$name

mkdir -p "$package/card/brainwave" "$package/firmware"
cp "$build/ce/brainwave.exe" "$build/ce/bwpower.dll" "$package/card/brainwave/"
cp "$build/firmware/brainwave.bin" "$package/card/brainwave/"
cp "$repository/ce/ReadMe.txt" "$package/card/brainwave/"
cp "$build/firmware/brainwave.uf2" "$build/firmware/partitions.uf2" "$package/firmware/"
cp "$repository/README.md" "$repository/LICENSE" "$package/"
cp -R "$repository/docs" "$package/docs"
(cd "$package" && find . -type f ! -name SHA256SUMS | sort | xargs sha256sum > SHA256SUMS)
rm -f "$build/$name.zip"
(cd "$stage" && find "$name" | sort | zip -q -X -@ "$build/$name.zip")
echo "$build/$name.zip"
