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
#
# The Linux binary ends up in Jellyfin containers and on NAS boxes, so it has
# to start on any x86-64 CPU and on a glibc as old as Ubuntu 22.04's.
set -e

BIN=${1:?usage: check-portable.sh <binary>}
GLIBC=2.35

if readelf -n "$BIN" | grep -E 'x86 ISA needed:.*x86-64-v[234]'; then
    echo "$BIN only starts on a newer CPU than plain x86-64"
    exit 1
fi

need=$(objdump -T "$BIN" | grep -o 'GLIBC_[0-9][0-9.]*' | cut -d_ -f2 | sort -Vu | tail -1)
echo "$BIN needs glibc $need"
if [ "$(printf '%s\n' "$need" "$GLIBC" | sort -V | tail -1)" != "$GLIBC" ]; then
    echo "that is newer than glibc $GLIBC"
    exit 1
fi
