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
# Somebody who downloads the binary on its own has no onnxruntime beside it.
# That has to still decode a film and come back with an answer, off libfvad.
set -e

LAPSE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
WORK=$(mktemp -d)
cd "$WORK"

espeak-ng -w line.wav -s 150 "The quick brown fox jumps over the lazy dog near the river bank."
spoken=$(ffprobe -v error -show_entries format=duration -of csv=p=0 line.wav)
cue=$(python3 -c "print(int(float('$spoken') * 1000))")

inputs=""
filters=""
chain=""
for i in $(seq 0 7); do
    inputs="$inputs -i line.wav"
    filters="$filters[$i:a]aresample=16000,apad=whole_dur=8[a$i];"
    chain="$chain[a$i]"
done

ffmpeg -v error -y $inputs -filter_complex \
    "${filters}${chain}concat=n=8:v=0:a=1,adelay=2000|2000,apad=pad_dur=2[out]" \
    -map "[out]" -ar 16000 -ac 1 audio.wav
ffmpeg -v error -y -f lavfi -i color=c=black:s=320x240:r=24 -i audio.wav \
    -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -shortest video.mkv

python3 -c "
def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return '%02d:%02d:%02d,%03d' % (h, m, s, ms)
out = []
for i in range(8):
    start = 2000 + i * 8000 + 4200
    out.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + $cue), i + 1))
open('late.srt', 'w').write('\n'.join(out))
"

echo "== no onnxruntime anywhere near this binary"
output=$("$LAPSE" video.mkv late.srt --no-backup)
echo "$output"

if ! echo "$output" | grep -q "using libfvad"; then
    echo "FAIL: it did not say it was falling back"
    exit 1
fi

offset=$(echo "$output" | sed -n 's/.*offset=\(-\{0,1\}[0-9]*\)ms.*/\1/p')
if [ -z "$offset" ]; then
    echo "FAIL: no offset came back"
    exit 1
fi
if [ "$offset" -gt -3500 ] || [ "$offset" -lt -5000 ]; then
    echo "FAIL: offset ${offset}ms is nowhere near the -4200ms it was shifted by"
    exit 1
fi

cd /
rm -rf "$WORK"
echo "fallback looks good, offset was ${offset}ms"
