#!/usr/bin/env bash
# Collect MinGW DLLs needed to run a CGO OpenCV binary beside the .exe.
# Must run under MSYS2 MINGW64 (not Git Bash): /mingw64 must be mounted.
# Usage: collect-mingw-opencv-dlls.sh <exe> <dest-dir>
set -euo pipefail

exe=${1:?exe path}
dest=${2:?dest dir}
mkdir -p "$dest"

if ! command -v ldd >/dev/null 2>&1; then
  echo "ldd not found (need MSYS2/MinGW shell)" >&2
  exit 1
fi

if [ ! -d /mingw64/bin ]; then
  echo "/mingw64/bin missing — run under MSYS2 MINGW64, not Git Bash" >&2
  exit 1
fi

count=0
while read -r f; do
  [ -n "$f" ] || continue
  [ -f "$f" ] || continue
  cp -f "$f" "$dest/"
  count=$((count + 1))
done < <(ldd "$exe" | awk '/\/mingw64\// {print $3}' | sort -u)

# Always include all OpenCV module DLLs (tracking loads siblings at runtime).
shopt -s nullglob
opencv_glob=(/mingw64/bin/libopencv_*.dll)
if [ ${#opencv_glob[@]} -eq 0 ]; then
  echo "no /mingw64/bin/libopencv_*.dll — install mingw-w64-x86_64-opencv" >&2
  exit 1
fi
for f in "${opencv_glob[@]}"; do
  cp -f "$f" "$dest/"
  count=$((count + 1))
done

n=$(ls -1 "$dest"/*.dll 2>/dev/null | wc -l | tr -d ' ')
echo "Collected ${n} DLLs into $dest (ldd hits≈${count})"
if [ "$n" -lt 20 ]; then
  echo "too few DLLs ($n) — OpenCV link likely missing or wrong shell" >&2
  exit 1
fi
