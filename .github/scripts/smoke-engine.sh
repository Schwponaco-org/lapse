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

SOLID=0
UNSURE=3

timestamp() {
    printf "%02d:%02d:%02d,%03d" $(($1 / 3600000)) $(($1 % 3600000 / 60000)) $(($1 % 60000 / 1000)) $(($1 % 1000))
}

run() {
    set +e
    output=$("$LAPSE" "$@" 2>&1)
    status=$?
    set -e
}

fail() {
    echo "FAIL: $1"
    [ -n "$output" ] && echo "$output"
    exit 1
}

for i in $(seq 0 39); do
    start=$((5000 + i * 7000))
    end=$((start + 3000))
    cue=$((i + 1))
    printf "%d\n%s --> %s\nline %d\n\n" "$cue" "$(timestamp $start)" "$(timestamp $end)" "$cue" >> reference.srt
    printf "%d\n%s --> %s\nline %d\n\n" "$cue" "$(timestamp $((start + SHIFT)))" "$(timestamp $((end + SHIFT)))" "$cue" >> late.srt
done
cp late.srt pristine.srt
cp late.srt sidecar.srt

cue=0
for i in $(seq 0 39); do
    start=$((5000 + i * 7000))
    end=$((start + 3000))
    by=$SHIFT
    [ $i -ge 20 ] && by=$((SHIFT + 2500))
    cue=$((cue + 1))
    printf "%d\n%s --> %s\nline %d\n\n" "$cue" "$(timestamp $((start + by)))" "$(timestamp $((end + by)))" "$i" >> recut.srt
    if [ $((i % 4)) = 1 ]; then
        cue=$((cue + 1))
        printf "%d\n%s --> %s\n- and another\n\n" "$cue" "$(timestamp $((start + by + 300)))" "$(timestamp $((end + by - 200)))" >> recut.srt
    fi
done

echo "== a subtitle that is ${SHIFT}ms late"
cp pristine.srt unsure.srt
run reference.srt unsure.srt
echo "$output"

if [ "$status" != "$UNSURE" ]; then
    fail "expected exit $UNSURE with only subtitles to go on, got $status"
fi
if ! echo "$output" | grep -q -- "offset=-${SHIFT}ms"; then
    fail "expected offset=-${SHIFT}ms"
fi
if [ ! -f unsure.lapse-unsure.srt ]; then
    fail "no sidecar was written"
fi
if [ "$(grep -m1 -- '-->' unsure.lapse-unsure.srt)" != "$(grep -m1 -- '-->' reference.srt)" ]; then
    fail "the sidecar does not line up with the reference"
fi
if ! cmp -s unsure.srt pristine.srt; then
    fail "the original was touched when the answer was only a guess"
fi
if [ -f unsure.srt.bak ]; then
    fail "a backup was written for a file that was never changed"
fi

echo "== --force overwrites the original and leaves a backup"
cp pristine.srt forced.srt
run reference.srt forced.srt --force
if [ "$status" != "$SOLID" ]; then
    fail "expected exit $SOLID with --force, got $status"
fi
if [ ! -f forced.srt.bak ]; then
    fail "no backup was written"
fi
if ! cmp -s forced.srt.bak pristine.srt; then
    fail "the backup is not the file we started with"
fi
if [ "$(grep -m1 -- '-->' forced.srt)" != "$(grep -m1 -- '-->' reference.srt)" ]; then
    fail "the corrected file does not line up with the reference"
fi

echo "== a VobSub .idx with a delay line"
{
    printf "# VobSub index file, v7 (do not modify this line!)\n#\t delay: [sign]hh:mm:ss:ms\n\nid: en, index: 0\ndelay: 00:00:01:000\n"
    for i in $(seq 0 39); do
        printf "timestamp: %s, filepos: %09x\n" "$(timestamp $((4000 + i * 7000 + SHIFT)) | tr , :)" $((i * 2048))
    done
} > delayed.idx
run reference.srt delayed.idx --force --no-backup
if [ "$status" != "$SOLID" ]; then
    fail "expected exit $SOLID for an .idx with a delay, got $status"
fi
if ! grep -q "^timestamp: 00:00:04:000, filepos: 000000000$" delayed.idx; then
    fail "the .idx did not keep its delay out of the timestamps it wrote"
fi

echo "== --strict refuses instead of writing beside the original"
cp pristine.srt strict.srt
run reference.srt strict.srt --strict
if [ "$status" != "$UNSURE" ]; then
    fail "expected exit $UNSURE with --strict, got $status"
fi
if [ -f strict.lapse-unsure.srt ] || [ -f strict.srt.bak ]; then
    fail "--strict wrote something anyway"
fi
if ! cmp -s strict.srt pristine.srt; then
    fail "--strict changed the original"
fi

echo "== two lines on screen at once do not cut the file in pieces"
run reference.srt recut.srt split --json --dry-run --strict
cues=$(echo "$output" | sed -n 's/.*"cues":\([0-9]*\).*/\1/p')
splits=$(echo "$output" | sed -n 's/.*"splits":\[\([0-9,]*\)\].*/\1/p')
if [ "$cues" != "50" ]; then
    fail "expected 50 cues in the report, got '$cues'"
fi
if [ "$splits" != "25" ]; then
    fail "expected one split at cue 25, got '$splits'"
fi
if ! echo "$output" | grep -q '"offsets":\[-4200,-6700\]'; then
    fail "expected the two parts to move by -4200 and -6700"
fi

echo "== writing a sidecar and leaving the original alone"
run reference.srt sidecar.srt --force --output fixed.srt --no-backup
if [ "$status" != "$SOLID" ]; then
    fail "expected exit $SOLID, got $status"
fi
if [ ! -f fixed.srt ] || [ -f sidecar.srt.bak ]; then
    fail "--output or --no-backup did not do what it says"
fi
if ! cmp -s sidecar.srt pristine.srt; then
    fail "the input was touched when it should not have been"
fi
if [ "$(grep -m1 -- '-->' fixed.srt)" != "$(grep -m1 -- '-->' reference.srt)" ]; then
    fail "--output did not line up with the reference"
fi

echo "== a format the engine does not read"
cp reference.srt unreadable.xyz
run reference.srt unreadable.xyz
if [ "$status" = "$SOLID" ]; then
    fail "an unreadable format should come back as an error"
fi

echo "== too few cues still answers in json"
head -8 pristine.srt > two.srt
run reference.srt two.srt --json
if [ "$status" != "2" ]; then
    fail "expected exit 2 for a file with two cues, got $status"
fi
if ! echo "$output" | grep -q '"reference":"none".*"written":false'; then
    fail "expected a json line saying nothing was written"
fi

echo "== an existing backup is never overwritten"
echo "the original" > guarded.srt.bak
cp pristine.srt guarded.srt
run reference.srt guarded.srt --force
if [ "$status" != "$SOLID" ]; then
    fail "expected exit $SOLID, got $status"
fi
if [ "$(cat guarded.srt.bak)" != "the original" ]; then
    fail "the first backup was overwritten"
fi

cd /
rm -rf "$WORK"
echo "engine looks good"
