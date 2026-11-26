#!/usr/bin/env bash
# AppImage de Linux x86_64 desde macOS (o desde cualquier sistema con Docker). En Apple Silicon necesita una
# máquina de colima con Rosetta:  colima start linux-build --vm-type vz --vz-rosetta --cpu 6 --memory 8
# La caché binaria de vcpkg va en un volumen de Docker para no recompilar LibRaw cada vez.
set -euo pipefail
cd "$(dirname "$0")"
docker build --platform linux/amd64 -t cdng2braw-linux-build packaging/linux
docker run --rm --platform linux/amd64 -v "$PWD":/src -v cdng2braw-vcpkg-cache:/root/.cache/vcpkg \
    -e JOBS="${JOBS:-}" cdng2braw-linux-build bash -c 'JOBS=${JOBS:-$(nproc)} /src/packaging/linux/build-appimage.sh'
