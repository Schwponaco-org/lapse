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
set -e

PREFIX=${1:?usage: build-ffmpeg.sh <prefix>}
VERSION=${FFMPEG_VERSION:-7.1}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

curl -fsSL --retry 5 --retry-all-errors --retry-delay 2 --connect-timeout 20 -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz"
tar xf ffmpeg.tar.xz
cd "ffmpeg-${VERSION}"

TOOLS=""
if [ -n "$CC" ]; then TOOLS="--cc=$CC"; fi
if [ -n "$CXX" ]; then TOOLS="$TOOLS --cxx=$CXX"; fi

./configure \
    --prefix="$PREFIX" \
    $TOOLS \
    --enable-static \
    --disable-shared \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-autodetect \
    --disable-everything \
    --enable-zlib \
    --enable-avformat \
    --enable-avcodec \
    --enable-avutil \
    --enable-swresample \
    --enable-demuxer=matroska,mov,avi,mp4,ogg,wav,flac,aac,mpegts \
    --enable-decoder=aac,ac3,eac3,mp3,flac,vorbis,opus,dca,truehd,pcm_s16le,pcm_s16be,pcm_s24le,pcm_f32le,pcm_f32be \
    --enable-parser=aac,mp3,flac,vorbis,opus,ac3 \
    --enable-protocol=file

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install

for lib in libavformat libavcodec libavutil libswresample; do
    if [ ! -f "$PREFIX/lib/${lib}.a" ]; then
        echo "Missing $PREFIX/lib/${lib}.a, the static FFmpeg build failed"
        exit 1
    fi
done
ls -la "$PREFIX"/lib/*.a
