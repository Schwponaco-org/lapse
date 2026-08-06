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
# Syncs a subtitle against a reference subtitle with a shift we picked
# ourselves, so we know exactly what the binary is supposed to come back with.
set -e

LAPSE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SHIFT=4200
WORK=$(mktemp -d)
cd "$WORK"

timestamp() {
    printf "%02d:%02d:%02d,%03d" $(($1 / 3600000)) $(($1 % 3600000 / 60000)) $(($1 % 60000 / 1000)) $(($1 % 1000))
}

for i in $(seq 0 39); do
    start=$((5000 + i * 7000))
    end=$((start + 3000))
    cue=$((i + 1))
    printf "%d\n%s --> %s\nline %d\n\n" "$cue" "$(timestamp $start)" "$(timestamp $end)" "$cue" >> reference.srt
    printf "%d\n%s --> %s\nline %d\n\n" "$cue" "$(timestamp $((start + SHIFT)))" "$(timestamp $((end + SHIFT)))" "$cue" >> late.srt
done
cp late.srt sidecar.srt

echo "== a subtitle that is ${SHIFT}ms late"
output=$("$LAPSE" reference.srt late.srt)
echo "$output"

if ! echo "$output" | grep -q -- "offset=-${SHIFT}ms"; then
    echo "FAIL: expected offset=-${SHIFT}ms"
    exit 1
fi
if [ ! -f late.srt.bak ]; then
    echo "FAIL: no backup was written"
    exit 1
fi
if [ "$(grep -m1 -- '-->' late.srt)" != "$(grep -m1 -- '-->' reference.srt)" ]; then
    echo "FAIL: the corrected file does not line up with the reference"
    exit 1
fi

echo "== writing a sidecar and leaving the original alone"
"$LAPSE" reference.srt sidecar.srt --output fixed.srt --no-backup > /dev/null
if [ ! -f fixed.srt ] || [ -f sidecar.srt.bak ]; then
    echo "FAIL: --output or --no-backup did not do what it says"
    exit 1
fi
if [ "$(grep -m1 -- '-->' sidecar.srt)" != "$(grep -m1 -- '-->' late.srt.bak)" ]; then
    echo "FAIL: the input was touched when it should not have been"
    exit 1
fi

echo "== a format the engine does not read"
cp reference.srt unreadable.xyz
if "$LAPSE" reference.srt unreadable.xyz > /dev/null 2>&1; then
    echo "FAIL: an unreadable format should come back as an error"
    exit 1
fi

echo "== an existing backup is never overwritten"
echo "the original" > guarded.srt.bak
cp late.srt.bak guarded.srt
"$LAPSE" reference.srt guarded.srt > /dev/null
if [ "$(cat guarded.srt.bak)" != "the original" ]; then
    echo "FAIL: the first backup was overwritten"
    exit 1
fi

cd /
rm -rf "$WORK"
echo "engine looks good"
