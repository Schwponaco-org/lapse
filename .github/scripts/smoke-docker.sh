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
# Runs the image the way somebody would actually run it and checks that a
# library full of out of sync subtitles comes out the other side corrected.
set -e

IMAGE=$1
SCRIPTS=$(cd "$(dirname "$0")" && pwd)
MEDIA=lapse-ci-media-$$
DATA=lapse-ci-data-$$
CONTAINER=lapse-ci-$$

cleanup() {
    docker rm -f $CONTAINER > /dev/null 2>&1 || true
    docker volume rm $MEDIA $DATA > /dev/null 2>&1 || true
}
trap cleanup EXIT

wait_for() {
    text=$1
    limit=${2:-180}
    waited=0
    while [ $waited -lt $limit ]; do
        if docker logs $CONTAINER 2>&1 | grep -qF "$text"; then
            return 0
        fi
        sleep 3
        waited=$((waited + 3))
    done
    echo "FAIL: gave up waiting for: $text"
    docker logs $CONTAINER
    exit 1
}

docker volume create $MEDIA > /dev/null
docker volume create $DATA > /dev/null

echo "== building a library to work on"
docker run --rm -v $MEDIA:/media -v "$SCRIPTS:/scripts:ro" --entrypoint /scripts/make-media.sh $IMAGE

echo "== starting the container the way the compose file does"
docker run -d --name $CONTAINER -v $MEDIA:/media -v $DATA:/data \
    -e PUID=1000 -e PGID=1000 -e SCAN_INTERVAL=0 $IMAGE > /dev/null

wait_for "Watching for new files..."
docker logs $CONTAINER

echo "== checking what it did"
docker run --rm -v $DATA:/data -v $MEDIA:/media -v "$SCRIPTS:/scripts:ro" \
    --entrypoint python3 $IMAGE /scripts/check-db.py

if docker logs $CONTAINER 2>&1 | grep -q "@eaDir"; then
    echo "FAIL: it went into @eaDir"
    exit 1
fi

if ! docker logs $CONTAINER 2>&1 | grep -q "reference=embedded"; then
    echo "FAIL: the film with a subtitle track inside it was synced off the audio"
    exit 1
fi
if ! docker logs $CONTAINER 2>&1 | grep -q "reference=vad"; then
    echo "FAIL: the films without a track were not synced off their audio"
    exit 1
fi
if [ "$(docker run --rm --entrypoint /app/lapse $IMAGE --vad)" != "silero" ]; then
    echo "FAIL: the image does not carry a Silero the engine can load"
    exit 1
fi
echo "both references were used, the embedded track and the audio"

echo "== dropping a new episode in while it runs"
docker run --rm -v $MEDIA:/media -v "$SCRIPTS:/scripts:ro" --entrypoint /scripts/make-media.sh $IMAGE extra
wait_for "Show.S01E03.english.srt"
wait_for "Show.S01E03.english.srt: "
sleep 5
docker run --rm -v $DATA:/data -v $MEDIA:/media -v "$SCRIPTS:/scripts:ro" \
    --entrypoint python3 $IMAGE /scripts/check-db.py extra

echo "== everything the container writes belongs to the right user"
docker run --rm -v $MEDIA:/media -v $DATA:/data --entrypoint sh $IMAGE -c '
found=$(find /media /data ! -user 1000 -type f | head)
if [ -n "$found" ]; then
    echo "FAIL: these are not owned by 1000:"
    echo "$found"
    exit 1
fi
echo "all files owned by 1000"'

echo "== stopping should be quick and clean"
start=$(date +%s)
docker stop $CONTAINER > /dev/null
elapsed=$(($(date +%s) - start))
if [ $elapsed -gt 8 ]; then
    echo "FAIL: took ${elapsed}s to stop, it is not handling the signal"
    exit 1
fi
if ! docker logs $CONTAINER 2>&1 | grep -q "Shutting down"; then
    echo "FAIL: it did not shut down cleanly"
    exit 1
fi
echo "stopped in ${elapsed}s"

echo "== starting again should not redo finished work"
# docker keeps the log from before the restart, so only look at what comes after
before=$(docker logs $CONTAINER 2>&1 | wc -l)
docker start $CONTAINER > /dev/null
sleep 25
if docker logs $CONTAINER 2>&1 | tail -n +$((before + 1)) | grep -q "Syncing:"; then
    echo "FAIL: it synced something a second time"
    docker logs $CONTAINER 2>&1 | tail -n +$((before + 1))
    exit 1
fi
echo "nothing was done twice"

echo "docker image looks good"
