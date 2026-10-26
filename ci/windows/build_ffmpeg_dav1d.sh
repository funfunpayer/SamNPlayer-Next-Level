#!/usr/bin/env bash
# FFmpeg for the Windows package with the dav1d AV1 decoder (Owner 26 Oct).
#
# Qt's own FFmpeg for Windows is built without --enable-libdav1d, so AV1
# video plays only on a GPU that decodes AV1 and shows no picture
# elsewhere (0.7 played it: WebView2 brings dav1d). This builds the same
# FFmpeg as Qt 6.7.3 (n7.0.2, Qt's configure options from
# qt/qt5 coin/provisioning/common/shared/ffmpeg_config_options.txt, MSVC,
# shared) plus libdav1d from vcpkg. Its DLLs keep Qt's names, so the
# package uses them in place of Qt's.
#
# Runs in MSYS2 bash with the MSVC environment inherited
# (MSYS2_PATH_TYPE=inherit) after CMake's configure step installed dav1d
# into build/vcpkg_installed. Writes $GITHUB_WORKSPACE/.ffmpeg-dav1d.
set -euo pipefail

version=n7.0.2
sha1=e017c72dd84a9bac1519eaa33c203b82dd850bc0  # as pinned by Qt 6.7.3
qt_options="--disable-programs --disable-doc --disable-debug --enable-network --disable-lzma --enable-pic --disable-vulkan --disable-v4l2-m2m --disable-decoder=truemotion1"

ws=$(cygpath -u "$GITHUB_WORKSPACE")
out=$(cygpath -m "$ws/.ffmpeg-dav1d")
deps="$ws/build/vcpkg_installed/x64-windows"
test -f "$deps/lib/pkgconfig/dav1d.pc" || { echo "dav1d from vcpkg is missing: $deps" >&2; exit 1; }

pacman -S --noconfirm --needed make nasm pkgconf diffutils >/dev/null
# MSYS2's link.exe would shadow MSVC's linker.
[ -f /usr/bin/link.exe ] && mv /usr/bin/link.exe /usr/bin/link-msys.exe

tmp=$(mktemp -d)
curl -fsSL --retry 3 -o "$tmp/ffmpeg.tar.gz" "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/$version.tar.gz"
echo "$sha1  $tmp/ffmpeg.tar.gz" | sha1sum -c -
tar -xzf "$tmp/ffmpeg.tar.gz" -C "$tmp"
cd "$tmp/FFmpeg-$version"

# Windows-style paths in the .pc file, so cl.exe gets -IC:/... not /c/...
export PKG_CONFIG_PATH
PKG_CONFIG_PATH=$(cygpath -m "$deps/lib/pkgconfig")
# shellcheck disable=SC2086
./configure --prefix="$out" $qt_options --enable-libdav1d --toolchain=msvc --enable-shared --disable-static
make -j"$(nproc)"
make install
cp "$deps/bin/dav1d.dll" "$(cygpath -u "$out")/bin/"
cp LICENSE.md COPYING.LGPLv2.1 "$(cygpath -u "$out")/"
ls "$(cygpath -u "$out")/bin"
