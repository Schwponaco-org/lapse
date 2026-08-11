#!/bin/bash
# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (r-stisen)
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

PREFIX=${1:?usage: build-libfvad.sh <prefix>}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

git clone --depth 1 https://github.com/dpirch/libfvad.git
cd libfvad
autoreconf -i
./configure --prefix="$PREFIX" --enable-static --disable-shared
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install

if [ ! -f "$PREFIX/lib/libfvad.a" ]; then
    echo "Missing $PREFIX/lib/libfvad.a, the static libfvad build failed"
    exit 1
fi
