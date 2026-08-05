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
# Builds a library to test against, inside the image so it has ffmpeg.
# The audio is eight bursts of band limited noise sitting in eight second
# slots, which the voice detector picks up the same way it picks up speech.
set -e

SLOT=8
LEAD=2
BURSTS=8

build_video() {
    ffmpeg -v error -y -f lavfi -i "anoisesrc=color=pink:r=16000:d=3:a=0.6" \
        -af "highpass=f=300,lowpass=f=3000,tremolo=f=5:d=0.9" /tmp/burst.wav

    inputs=""
    filters=""
    chain=""
    for i in $(seq 0 $((BURSTS - 1))); do
        inputs="$inputs -i /tmp/burst.wav"
        filters="$filters[$i:a]apad=whole_dur=$SLOT[a$i];"
        chain="$chain[a$i]"
    done

    ffmpeg -v error -y $inputs -filter_complex \
        "${filters}${chain}concat=n=$BURSTS:v=0:a=1,adelay=${LEAD}000|${LEAD}000,apad=pad_dur=2[out]" \
        -map "[out]" -ar 16000 -ac 1 /tmp/audio.wav

    ffmpeg -v error -y -f lavfi -i color=c=black:s=320x240:r=24 -i /tmp/audio.wav \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -shortest /tmp/video.mkv
}

write_srt() {
    python3 -c "
import sys
path, shift = sys.argv[1], int(sys.argv[2])
def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return '%02d:%02d:%02d,%03d' % (h, m, s, ms)
cues = []
for i in range($BURSTS):
    start = $LEAD * 1000 + i * $SLOT * 1000 + shift
    cues.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + 3000), i + 1))
open(path, 'w').write('\n'.join(cues))
" "$1" "$2"
}

build_video

if [ "$1" = "extra" ]; then
    # A release turning up while the watcher is already running
    mkdir -p "/media/TV/Show/Season 01/Subs"
    cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E03.1080p.WEB-DL.mkv"
    write_srt "/media/TV/Show/Season 01/Subs/Show.S01E03.english.srt" 4200
    chown -R 1000:1000 /media
    echo "added a new episode"
    exit 0
fi

mkdir -p "/media/Movies/Test Movie (2024)"
cp /tmp/video.mkv "/media/Movies/Test Movie (2024)/Test Movie (2024).mkv"
write_srt "/media/Movies/Test Movie (2024)/Test Movie (2024).en.srt" 4200

# Subtitles in their own folder, the way a lot of releases ship
mkdir -p "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Subs"
cp /tmp/video.mkv "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Scene.Release.2024.1080p.BluRay.x264-GRP.mkv"
write_srt "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Subs/2_English.srt" 4200

# Two episodes side by side, each subtitle has to find its own one
mkdir -p "/media/TV/Show/Season 01"
cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E01.1080p.WEB-DL.mkv"
cp /tmp/video.mkv "/media/TV/Show/Season 01/Show.S01E02.1080p.WEB-DL.mkv"
write_srt "/media/TV/Show/Season 01/Show.S01E01.en.srt" 4200
write_srt "/media/TV/Show/Season 01/Show.S01E02.da.srt" 4200

# Things that should be left where they are
mkdir -p "/media/TV/Show/Season 01/@eaDir" "/media/Movies/Orphan"
write_srt "/media/TV/Show/Season 01/@eaDir/Show.S01E01.srt" 0
write_srt "/media/Movies/Orphan/orphan.srt" 0

chown -R 1000:1000 /media
find /media -type f -exec touch -d "10 minutes ago" {} +
echo "library ready"
find /media -type f | sort
