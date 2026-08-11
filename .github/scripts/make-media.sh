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
#
# Builds a library to test against, inside the image so it has ffmpeg.
# The audio is synthesised speech, because the voice detector is a speech
# model now and will not be fooled by noise that only looks like talking.
# One film also carries a subtitle track inside it, which is the reference
# LAPSE reaches for before it decodes any audio at all.
set -e

LINE="The quick brown fox jumps over the lazy dog near the river bank."
LEAD=2
BURSTS=20
SHIFT=4200

if ! command -v espeak-ng > /dev/null; then
    apt-get update -qq > /dev/null
    apt-get install -y -qq espeak-ng > /dev/null
fi

build_media() {
    espeak-ng -w /tmp/spoken.wav -s 150 "$LINE"
    trim="silenceremove=start_periods=1:start_threshold=-40dB:detection=peak"
    ffmpeg -v error -y -i /tmp/spoken.wav -ar 16000 -ac 1 \
        -af "${trim},areverse,${trim},areverse" /tmp/line.wav
    spoken=$(ffprobe -v error -show_entries format=duration -of csv=p=0 /tmp/line.wav)
    CUE=$(python3 -c "print(int(float('$spoken') * 1000))")

    SLOTS=$(python3 -c "
import random
random.seed(5)
print(' '.join(str(random.randint(6, 11)) for _ in range($BURSTS)))
")

    inputs=""
    filters=""
    chain=""
    i=0
    for slot in $SLOTS; do
        inputs="$inputs -i /tmp/line.wav"
        filters="$filters[$i:a]aresample=16000,apad=whole_dur=$slot[a$i];"
        chain="$chain[a$i]"
        i=$((i + 1))
    done

    ffmpeg -v error -y $inputs -filter_complex \
        "${filters}${chain}concat=n=$BURSTS:v=0:a=1,adelay=${LEAD}000|${LEAD}000,apad=pad_dur=2[out]" \
        -map "[out]" -ar 16000 -ac 1 /tmp/audio.wav

    ffmpeg -v error -y -f lavfi -i color=c=black:s=320x240:r=24 -i /tmp/audio.wav \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -shortest /tmp/video.mkv

    # A track inside the file has to carry a real film's worth of cues before
    # LAPSE will trust it, so this one is written a cue a second
    write_srt /tmp/embedded.srt 0 60 1000 800 400
    ffmpeg -v error -y -i /tmp/video.mkv -i /tmp/embedded.srt -map 0 -map 1 \
        -c copy -c:s srt /tmp/video-with-track.mkv
}

# path, shift, cues, gap between cues, how long each one lasts, gap jitter
write_srt() {
    python3 -c "
import random, sys
path, shift, cues, gap, length = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
jitter = int(sys.argv[6]) if len(sys.argv) > 6 else 0
random.seed(11)
def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return '%02d:%02d:%02d,%03d' % (h, m, s, ms)
out = []
at = $LEAD * 1000
for i in range(cues):
    start = at + shift
    out.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + length), i + 1))
    at += gap + (random.randint(-jitter, jitter) if jitter else 0)
open(path, 'w').write('\n'.join(out))
" "$1" "$2" "$3" "$4" "$5" "${6:-0}"
}

speech_srt() {
    python3 -c "
import sys
path, shift = sys.argv[1], int(sys.argv[2])
slots = [int(x) * 1000 for x in '$SLOTS'.split()]
def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return '%02d:%02d:%02d,%03d' % (h, m, s, ms)
out = []
at = $LEAD * 1000
for i, slot in enumerate(slots):
    start = at + shift
    out.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + $CUE), i + 1))
    at += slot
open(path, 'w').write('\n'.join(out))
" "$1" "$2"
}

build_media

if [ "$1" = "extra" ]; then
    # A release turning up while the watcher is already running
    mkdir -p "/media/TV/Show/Season 01/Subs"
    cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E03.1080p.WEB-DL.mkv"
    speech_srt "/media/TV/Show/Season 01/Subs/Show.S01E03.english.srt" $SHIFT
    chown -R 1000:1000 /media
    echo "added a new episode"
    exit 0
fi

# This one has a subtitle track inside it, so it never gets as far as the audio
mkdir -p "/media/Movies/Test Movie (2024)"
cp /tmp/video-with-track.mkv "/media/Movies/Test Movie (2024)/Test Movie (2024).mkv"
write_srt "/media/Movies/Test Movie (2024)/Test Movie (2024).en.srt" $SHIFT 60 1000 800 400

# Subtitles in their own folder, the way a lot of releases ship
mkdir -p "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Subs"
cp /tmp/video.mkv "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Scene.Release.2024.1080p.BluRay.x264-GRP.mkv"
speech_srt "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Subs/2_English.srt" $SHIFT

# Two episodes side by side, each subtitle has to find its own one
mkdir -p "/media/TV/Show/Season 01"
cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E01.1080p.WEB-DL.mkv"
cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E02.1080p.WEB-DL.mkv"
speech_srt "/media/TV/Show/Season 01/Show.S01E01.en.srt" $SHIFT
speech_srt "/media/TV/Show/Season 01/Show.S01E02.da.srt" $SHIFT

# Things that should be left where they are
mkdir -p "/media/TV/Show/Season 01/@eaDir" "/media/Movies/Orphan"
speech_srt "/media/TV/Show/Season 01/@eaDir/Show.S01E01.srt" 0
speech_srt "/media/Movies/Orphan/orphan.srt" 0

chown -R 1000:1000 /media
find /media -type f -exec touch -d "10 minutes ago" {} +
echo "library ready"
find /media -type f | sort
