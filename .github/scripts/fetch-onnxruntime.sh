#!/bin/bash
# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
# Usage: fetch-onnxruntime.sh <target> <version> <dest>
set -e

TARGET=${1:?usage: fetch-onnxruntime.sh <target> <version> <dest>}
VERSION=${2:?usage: fetch-onnxruntime.sh <target> <version> <dest>}
DEST=${3:?usage: fetch-onnxruntime.sh <target> <version> <dest>}

MODEL_TAG=v6.2.1
MODEL_SHA=1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3

mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

BASE="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}"
DIR="onnxruntime-${TARGET}-${VERSION}"

case "$TARGET" in
    win-*)
        curl -fsSL --retry 10 --retry-all-errors --retry-delay 30 --connect-timeout 20 -o ort.zip "${BASE}/${DIR}.zip"
        # MSYS2's unzip has been seen mangling this binary, likely a text mode line
        # ending conversion, so extract it with Windows' own tool instead
        pwsh=powershell.exe
        command -v "$pwsh" > /dev/null 2>&1 || pwsh="/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
        "$pwsh" -NoProfile -Command "Expand-Archive -LiteralPath '$(cygpath -w "$PWD/ort.zip")' -DestinationPath '$(cygpath -w "$PWD")' -Force"
        cp "${DIR}/lib/onnxruntime.dll" "$DEST/onnxruntime.dll"
        ;;
    osx-*)
        curl -fsSL --retry 10 --retry-all-errors --retry-delay 30 --connect-timeout 20 -o ort.tgz "${BASE}/${DIR}.tgz"
        tar xf ort.tgz "${DIR}/lib/libonnxruntime.${VERSION}.dylib"
        cp "${DIR}/lib/libonnxruntime.${VERSION}.dylib" "$DEST/libonnxruntime.dylib"
        ;;
    linux-musl)
        CACHE=${ORT_CACHE:-$WORK/built}
        if [ ! -f "$CACHE/libonnxruntime.so.${VERSION}" ]; then
            curl -fsSL --retry 10 --retry-all-errors --retry-delay 30 --connect-timeout 20 -o ort.tgz \
                "https://github.com/microsoft/onnxruntime/archive/refs/tags/v${VERSION}.tar.gz"
            tar xf ort.tgz
            cmake -S "onnxruntime-${VERSION}/cmake" -B ort-build -G Ninja \
                -DCMAKE_BUILD_TYPE=MinSizeRel \
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
                -DCMAKE_CXX_FLAGS="-Wno-error -U_FORTIFY_SOURCE" \
                -DCMAKE_SHARED_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--strip-all" \
                -Donnxruntime_BUILD_SHARED_LIB=ON \
                -Donnxruntime_BUILD_UNIT_TESTS=OFF \
                -Donnxruntime_ENABLE_PYTHON=OFF \
                -Donnxruntime_DISABLE_ML_OPS=ON
            cmake --build ort-build --target onnxruntime
            mkdir -p "$CACHE"
            cp "ort-build/libonnxruntime.so.${VERSION}" "$CACHE/"
        fi
        cp "$CACHE/libonnxruntime.so.${VERSION}" "$DEST/libonnxruntime.so"
        ;;
    linux-*)
        curl -fsSL --retry 10 --retry-all-errors --retry-delay 30 --connect-timeout 20 -o ort.tgz "${BASE}/${DIR}.tgz"
        tar xf ort.tgz "${DIR}/lib/libonnxruntime.so.${VERSION}"
        cp "${DIR}/lib/libonnxruntime.so.${VERSION}" "$DEST/libonnxruntime.so"
        ;;
    *)
        echo "Unknown onnxruntime target: $TARGET"
        exit 1
        ;;
esac

curl -fsSL --retry 10 --retry-all-errors --retry-delay 30 --connect-timeout 20 -o "$DEST/silero_vad.onnx" \
    "https://github.com/snakers4/silero-vad/raw/${MODEL_TAG}/src/silero_vad/data/silero_vad.onnx"

if command -v sha256sum > /dev/null; then
    echo "${MODEL_SHA}  ${DEST}/silero_vad.onnx" | sha256sum -c -
else
    echo "${MODEL_SHA}  ${DEST}/silero_vad.onnx" | shasum -a 256 -c -
fi

ls -la "$DEST"
