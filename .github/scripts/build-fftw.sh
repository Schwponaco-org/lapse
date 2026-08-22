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

PREFIX=${1:?usage: build-fftw.sh <prefix>}
VERSION=${FFTW_VERSION:-3.3.10}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

curl -fsSL --retry 5 --retry-all-errors --retry-delay 2 --connect-timeout 20 -o fftw.tar.gz "https://www.fftw.org/fftw-${VERSION}.tar.gz"
tar xf fftw.tar.gz
cd "fftw-${VERSION}"

./configure \
    --prefix="$PREFIX" \
    --enable-static \
    --disable-shared \
    --disable-fortran \
    --disable-doc

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install

if [ ! -f "$PREFIX/lib/libfftw3.a" ]; then
    echo "Missing $PREFIX/lib/libfftw3.a, the static FFTW build failed"
    exit 1
fi
