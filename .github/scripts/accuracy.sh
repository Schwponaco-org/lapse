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

# Builds videos whose speech sits at times we chose, then checks the engine
# puts the subtitle back where it started. Every case here is one that broke
# at some point, so a failure means something real came back.

set -e

if [ -z "$1" ]; then
    echo "usage: accuracy.sh <path to lapse>"
    exit 1
fi

LAPSE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
export LAPSE_CACHE="$WORK/cache"
cd "$WORK"

TOLERANCE=120
fails=0

speech() {
    if command -v espeak-ng > /dev/null; then
        espeak-ng -w line.wav -s 150 "The quick brown fox jumps over the lazy dog near the river bank."
    elif command -v say > /dev/null; then
        say -o line.aiff "The quick brown fox jumps over the lazy dog near the river bank."
        ffmpeg -v error -y -i line.aiff line.wav
    else
        echo "SKIP: no espeak-ng and no say, cannot make speech"
        exit 0
    fi
    trim="silenceremove=start_periods=1:start_threshold=-40dB:detection=peak"
    ffmpeg -v error -y -i line.wav -ar 16000 -ac 1 \
        -af "${trim},areverse,${trim},areverse" spoken.wav
    mv spoken.wav line.wav
}

echo "== building fixtures"
speech
python3 "$HERE/make-fixtures.py" "$WORK"

build() {
    ffmpeg -v error -y -f lavfi -i color=c=black:s=160x120:r=5 -i "$1" \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -shortest "${@:3}" "$2"
}

build audio.wav plain.mkv       -c:a flac
build audio.wav aac51.mp4       -c:a aac -ac 6
build audio.wav mono.mkv        -c:a libopus -ac 1
ffmpeg -v error -y -i audio.wav -c:a pcm_s24le audio24.wav
build audio24.wav deep.mkv      -c:a flac -sample_fmt s32
ffmpeg -v error -y -i plain.mkv -c:v mpeg4 -c:a mp3 clip.avi
ffmpeg -v error -y -i aac51.mp4 -c copy -output_ts_offset 3600 -muxdelay 0 late.ts
build concat_audio.wav joined.mkv -c:a flac

# how far every cue ended up from where it should be
check() {
    local name="$1" video="$2" input="$3" truth="$4"
    shift 4
    rm -rf "$LAPSE_CACHE"
    cp "$input" work.srt

    local out rc
    set +e
    out=$("$LAPSE" "$video" work.srt "$@" --no-backup --no-embedded 2>&1)
    rc=$?
    set -e

    if [ $rc -ne 0 ]; then
        printf "  %-28s FAIL exit %d\n" "$name" "$rc"
        echo "$out" | sed 's/^/      /'
        fails=$((fails + 1))
        return
    fi

    local worst
    worst=$(python3 "$HERE/compare-srt.py" work.srt "$truth")
    if [ "$worst" -gt "$TOLERANCE" ]; then
        printf "  %-28s FAIL worst cue off by %sms\n" "$name" "$worst"
        echo "$out" | sed 's/^/      /'
        fails=$((fails + 1))
    else
        printf "  %-28s ok   (worst %sms)\n" "$name" "$worst"
    fi
}

# should refuse to touch the file and come back non zero
refuses() {
    local name="$1" video="$2" input="$3"
    rm -rf "$LAPSE_CACHE"
    cp "$input" work.srt

    set +e
    "$LAPSE" "$video" work.srt --no-backup --no-embedded > /dev/null 2>&1
    local rc=$?
    set -e

    if [ $rc -eq 0 ]; then
        printf "  %-28s FAIL wrote a result it should not trust\n" "$name"
        fails=$((fails + 1))
    elif ! cmp -s work.srt "$input"; then
        printf "  %-28s FAIL refused but changed the file anyway\n" "$name"
        fails=$((fails + 1))
    else
        printf "  %-28s ok   (exit %d, file untouched)\n" "$name" "$rc"
    fi
}

echo "== containers, codecs and channel layouts"
check "flac stereo mkv"       plain.mkv  shifted.srt truth.srt
check "aac 5.1 mp4"           aac51.mp4  shifted.srt truth.srt
check "opus mono mkv"         mono.mkv   shifted.srt truth.srt
check "24 bit flac, s32"      deep.mkv   shifted.srt truth.srt
check "mpeg4 mp3 avi"         clip.avi   shifted.srt truth.srt
check "mpegts starting at 1h" late.ts    shifted.srt truth.srt

echo "== the shift itself"
check "already correct"       plain.mkv  truth.srt   truth.srt
check "runs early"            plain.mkv  early.srt   truth.srt
check "runs late"             plain.mkv  shifted.srt truth.srt

echo "== files joined out of parts"
check "a part was cut out"    joined.mkv concat_gap.srt concat_truth.srt
check "subtitle restarts"     joined.mkv concat_ep.srt  concat_truth.srt

echo "== modes"
check "nosplit"               plain.mkv  shifted.srt truth.srt nosplit
check "split"                 plain.mkv  shifted.srt truth.srt split

echo "== knows when it does not know"
refuses "a different film"    joined.mkv unrelated.srt

echo "== the cache"
rm -rf "$LAPSE_CACHE"
cp shifted.srt work.srt
"$LAPSE" plain.mkv work.srt --no-backup --no-embedded > /dev/null || true
cp shifted.srt work.srt
if "$LAPSE" plain.mkv work.srt --no-backup --no-embedded 2>&1 | grep -q "Reusing"; then
    echo "  cache is reused              ok"
else
    echo "  cache is reused              FAIL second run did not reuse it"
    fails=$((fails + 1))
fi

cd /
rm -rf "$WORK"

if [ $fails -gt 0 ]; then
    echo "$fails case(s) failed"
    exit 1
fi
echo "accuracy looks good"
