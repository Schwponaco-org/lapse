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
# Usage: build-lapse.sh <deps-prefix> <output>
set -e

DEPS=${1:?usage: build-lapse.sh <deps-prefix> <output>}
OUT=${2:?usage: build-lapse.sh <deps-prefix> <output>}

ENGINE=$(cd "$(dirname "$0")/../../engine" && pwd)

mkdir -p "$(dirname "$OUT")"
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")

# -O3 with lto is worth a few percent and costs nothing but build time. no
# -march here on purpose, the binary has to run on whatever the user has
OPT="-O3 -flto -fno-math-errno"

SOURCES="main.cpp correlate.cpp align.cpp decoder.cpp srt_parser.cpp write_subtitle.cpp silero.cpp cuetext.cpp charset.cpp log.cpp"

STATIC="$DEPS/lib/libavformat.a
        $DEPS/lib/libavcodec.a
        $DEPS/lib/libswresample.a
        $DEPS/lib/libavutil.a
        $DEPS/lib/libfvad.a
        $DEPS/lib/libfftw3.a"

cd "$ENGINE"

case "$(uname -s)" in
    Darwin)
        ${CXX:-clang++} $OPT -std=c++20 \
            -I"$DEPS/include" \
            -o "$OUT" \
            $SOURCES \
            $STATIC \
            -lm -lz -lpthread \
            -framework CoreFoundation -framework CoreServices -framework Security
        ;;

    MINGW*|MSYS*|CLANG*|CYGWIN*)
        case "$OUT" in *.exe) ;; *) OUT="$OUT.exe" ;; esac
        ${WINDRES:-windres} lapse.rc -O coff -o lapse.rc.o
        ${CXX:-g++} $OPT -std=c++20 \
            -I"$DEPS/include" \
            -o "$OUT" \
            $SOURCES lapse.rc.o \
            $STATIC \
            -static \
            -lm -lz -lpthread \
            -lws2_32 -lbcrypt -lsecur32 -lole32 -luser32
        rm -f lapse.rc.o
        ;;

    *)
        ${CXX:-g++} $OPT -std=c++20 \
            -I"$DEPS/include" \
            -o "$OUT" \
            $SOURCES \
            -static-libgcc -static-libstdc++ \
            $STATIC \
            -lm -lpthread -lz -ldl
        ;;
esac

ls -la "$OUT"
