#!/bin/sh
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
# Usage: build-alpine.sh <name> <onnxruntime version>
set -e

NAME=${1:?usage: build-alpine.sh <name> <onnxruntime version>}
VERSION=${2:?usage: build-alpine.sh <name> <onnxruntime version>}

apk add --no-cache bash build-base cmake samurai python3 linux-headers curl git \
    autoconf automake libtool pkgconf nasm yasm zlib-dev zlib-static espeak-ng ffmpeg

.github/scripts/build-ffmpeg.sh "$HOME/deps"
.github/scripts/build-fftw.sh "$HOME/deps"
.github/scripts/build-libfvad.sh "$HOME/deps"

cmake -B build -DLAPSE_DEPS_PREFIX="$HOME/deps" \
      -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/stage"
cmake --build build -j

stage/lapse --formats
.github/scripts/smoke-engine.sh stage/lapse
.github/scripts/smoke-fallback.sh stage/lapse

ORT_CACHE="$PWD/ort-musl" .github/scripts/fetch-onnxruntime.sh linux-musl "$VERSION" stage
if [ "$(stage/lapse --vad)" != "silero" ]; then
    echo "onnxruntime was shipped but the binary did not load it"
    exit 1
fi

.github/scripts/accuracy.sh stage/lapse

.github/scripts/package.sh "lapse-$NAME" stage dist
cp stage/lapse "dist/lapse-$NAME"
