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
# Drives lapse-tui in a pseudo terminal at 80 by 24 the way a person would:
# sync a file from the list, a dry run, undo, a shift by hand, a resize and
# quit, and checks the files on disk after each. Give it a library to copy,
# such as the one make-media.sh builds, or it makes a small one itself.
#
# Usage: smoke-tui.sh <lapse-tui> [library]
set -e

TUI=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
export LAPSE_CACHE="$WORK/cache"
LIBRARY="$WORK/library"
SHIFT=4200

if [ -n "$2" ]; then
    cp -R "$2" "$LIBRARY"
    # Whatever was synced in there already goes back to how it was made
    find "$LIBRARY" -name '*.bak' | while read -r bak; do
        mv "$bak" "${bak%.bak}"
    done
else
    cd "$WORK"
    espeak-ng -w spoken.wav -s 150 "The quick brown fox jumps over the lazy dog near the river bank."
    trim="silenceremove=start_periods=1:start_threshold=-40dB:detection=peak"
    ffmpeg -v error -y -i spoken.wav -ar 16000 -ac 1 -af "${trim},areverse,${trim},areverse" line.wav
    cue=$(python3 -c "print(int(float('$(ffprobe -v error -show_entries format=duration -of csv=p=0 line.wav)') * 1000))")
    slots=$(python3 -c "
import random
random.seed(5)
print(' '.join(str(random.randint(6, 11)) for _ in range(20)))
")

    inputs=""
    filters=""
    chain=""
    i=0
    for slot in $slots; do
        inputs="$inputs -i line.wav"
        filters="$filters[$i:a]aresample=16000,apad=whole_dur=$slot[a$i];"
        chain="$chain[a$i]"
        i=$((i + 1))
    done
    ffmpeg -v error -y $inputs -filter_complex \
        "${filters}${chain}concat=n=$i:v=0:a=1,adelay=2000|2000,apad=pad_dur=2[out]" \
        -map "[out]" -ar 16000 -ac 1 audio.wav
    ffmpeg -v error -y -f lavfi -i color=c=black:s=320x240:r=24 -i audio.wav \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -shortest video.mkv

    late() {
        python3 -c "
import sys
slots = [int(x) * 1000 for x in '$slots'.split()]
def ts(ms):
    return '%02d:%02d:%02d,%03d' % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)
out, at = [], 2000
for i, slot in enumerate(slots):
    start = at + $SHIFT
    out.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + $cue), i + 1))
    at += slot
open(sys.argv[1], 'w').write('\n'.join(out))
" "$1"
    }

    season="$LIBRARY/TV/Show/Season 01"
    film="$LIBRARY/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP"
    mkdir -p "$season" "$film/Subs" "$LIBRARY/Movies/Orphan"
    cp video.mkv "$season/Show.S01E01.1080p.WEB-DL.mkv"
    cp video.mkv "$season/Show.S01E02.1080p.WEB-DL.mkv"
    cp video.mkv "$film/Scene.Release.2024.1080p.BluRay.x264-GRP.mkv"
    late "$season/Show.S01E01.en.srt"
    late "$season/Show.S01E02.da.srt"
    late "$film/Subs/2_English.srt"
    late "$LIBRARY/Movies/Orphan/orphan.srt"
    cd /
fi

python3 "$HERE/drive-tui.py" "$TUI" "$LIBRARY"

cd /
rm -rf "$WORK"
echo "tui looks good"
