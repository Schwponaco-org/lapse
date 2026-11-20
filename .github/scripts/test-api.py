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
# Run by hand against a running container, CI does not run it.
# Talks to the sync API of a running container the way Bazarr does.
#   test-api.py http://127.0.0.1:8080 /tmp/media [/movies]
# The films are made in the folder if they are not there yet (needs ffmpeg and
# espeak-ng on this machine). The last argument is where the server sees that
# folder, when it is mounted somewhere else. Use a server that has not heard
# these films before, the progress lines only come when nothing is cached.

import base64
import concurrent.futures
import http.client
import json
import os
import random
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

SHIFT = 4200
LEAD = 2000
TOLERANCE = 250

url = urllib.parse.urlparse(sys.argv[1])
media = os.path.abspath(sys.argv[2])
seen = sys.argv[3] if len(sys.argv) > 3 else media

failures = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures.append(what)


def stamp(ms):
    return "%02d:%02d:%02d,%03d" % (ms // 3600000, ms % 3600000 // 60000, ms % 60000 // 1000, ms % 1000)


def write_srt(starts, length):
    out = []
    for i, start in enumerate(starts):
        out.append("%d\n%s --> %s\nline %d\n" % (i + 1, stamp(start), stamp(start + length), i + 1))
    return "\n".join(out).encode()


def first_start(data):
    found = re.search(rb"(\d+):(\d+):(\d+),(\d+) -->", data)
    h, m, s, ms = (int(x) for x in found.groups())
    return ((h * 60 + m) * 60 + s) * 1000 + ms


def run(*command):
    subprocess.run(command, check=True, capture_output=True)


def make_media():
    work = tempfile.mkdtemp()
    run("espeak-ng", "-w", work + "/spoken.wav", "-s", "150",
        "The quick brown fox jumps over the lazy dog near the river bank.")
    run("ffmpeg", "-y", "-i", work + "/spoken.wav", "-ar", "16000", "-ac", "1", work + "/line.wav")
    spoken = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                             work + "/line.wav"], capture_output=True, text=True, check=True).stdout
    length = int(float(spoken) * 1000)

    random.seed(5)
    slots = [random.randint(6, 11) for _ in range(20)]
    command = ["ffmpeg", "-y"]
    filters = ""
    for i, slot in enumerate(slots):
        command += ["-i", work + "/line.wav"]
        filters += "[%d:a]aresample=16000,apad=whole_dur=%d[a%d];" % (i, slot, i)
    filters += "".join("[a%d]" % i for i in range(len(slots)))
    filters += "concat=n=%d:v=0:a=1,adelay=%d|%d,apad=pad_dur=2[out]" % (len(slots), LEAD, LEAD)
    run(*command, "-filter_complex", filters, "-map", "[out]", "-ar", "16000", "-ac", "1", work + "/audio.wav")
    run("ffmpeg", "-y", "-f", "lavfi", "-i", "color=c=black:s=320x240:r=24", "-i", work + "/audio.wav",
        "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest",
        media + "/video.mkv")

    # the same sound over and over, so a sync of it takes a good while
    run("ffmpeg", "-y", "-i", media + "/video.mkv", "-vn", "-c:a", "aac", "-b:a", "8k", work + "/short.mka")
    run("ffmpeg", "-y", "-stream_loop", "250", "-i", work + "/short.mka", "-c", "copy", media + "/long.mkv")
    for i in range(1, 5):
        shutil.copy(media + "/video.mkv", media + "/copy%d.mkv" % i)
    shutil.rmtree(work)

    starts = [LEAD]
    for slot in slots[:-1]:
        starts.append(starts[-1] + slot * 1000)
    with open(media + "/starts.json", "w") as file:
        json.dump({"starts": starts, "length": length}, file)


def request(method, path, body=None):
    conn = http.client.HTTPConnection(url.hostname, url.port, timeout=300)
    conn.request(method, path, json.dumps(body) if body is not None else None,
                 {"Content-Type": "application/json"})
    answer = conn.getresponse()
    text = answer.read().decode()
    conn.close()
    kind = answer.getheader("Content-Type")
    if kind == "application/x-ndjson":
        return answer.status, kind, [json.loads(line) for line in text.splitlines() if line]
    return answer.status, kind, json.loads(text)


def sync(**fields):
    body = {"video": None, "subtitle": None, "filename": "movie.srt", "reference": None,
            "reference_filename": None, "audio_track": None, "sub_track": None, "no_embedded": False}
    body.update(fields)
    if isinstance(body["subtitle"], bytes):
        body["subtitle"] = base64.b64encode(body["subtitle"]).decode()
    if isinstance(body["reference"], bytes):
        body["reference"] = base64.b64encode(body["reference"]).decode()
    return request("POST", "/api/sync", body)


def lapse_running():
    # the engine's command line is the same on the host as inside a container
    listing = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True).stdout
    return sum(1 for line in listing.splitlines() if seen + "/" in line and "--no-sidecar" in line)


def gone_within(seconds):
    until = time.time() + seconds
    while time.time() < until:
        if lapse_running() == 0:
            return True
        time.sleep(0.1)
    return False


if not os.path.exists(media + "/video.mkv"):
    os.makedirs(media, exist_ok=True)
    make_media()

with open(media + "/starts.json") as file:
    info = json.load(file)
starts, length = info["starts"], info["length"]
video = seen + "/video.mkv"
late = write_srt([s + SHIFT for s in starts], length)

print("== version")
status, kind, answer = request("GET", "/api/version")
check(status == 200 and re.fullmatch(r"\d+\.\d+\.\d+", answer.get("version", "")), "version is %s" % answer)

print("== a late subtitle against the video")
status, kind, lines = sync(video=video, subtitle=late)
check(status == 200 and kind == "application/x-ndjson", "200 and ndjson")
progress = [line for line in lines if "progress" in line]
check(len(progress) >= 2 and all(0 <= line["progress"] <= 100 and line["phase"] for line in progress),
      "progress lines came first: %s" % [(line["phase"], line["progress"]) for line in progress[:3]])
check(list(lines[-1]) == ["done", "subtitle"] and lines[-1]["done"]["verdict"] == "solid",
      "last line is done and solid")
fixed = base64.b64decode(lines[-1]["subtitle"] or "")
check(abs(first_start(fixed) - LEAD) <= TOLERANCE and abs(lines[-1]["done"]["offset_ms"] + SHIFT) <= TOLERANCE,
      "the subtitle came back %d ms earlier" % SHIFT)

print("== the same again, now from the cache")
status, kind, lines = sync(video=video, subtitle=late, no_embedded=True, audio_track=0)
check(status == 200 and lines[-1].get("subtitle") is not None, "still solid with the options on")

print("== against another subtitle")
status, kind, lines = sync(subtitle=late, reference=write_srt(starts, length), reference_filename="reference.srt")
check(status == 200 and lines[-1].get("done", {}).get("reference") == "subtitle", "used the reference")
check(lines[-1].get("subtitle") is not None and abs(first_start(base64.b64decode(lines[-1]["subtitle"])) - LEAD) <= TOLERANCE,
      "and moved it onto it")

print("== a subtitle that does not belong to the film")
random.seed(3)
at = 3000
wrong = []
for _ in range(100):
    wrong.append(at)
    at += random.randint(1500, 9000)
status, kind, lines = sync(video=video, subtitle=write_srt(wrong, 1500))
check(status == 200 and "done" in lines[-1] and lines[-1]["subtitle"] is None, "no subtitle came back")
check(lines[-1]["done"]["verdict"] != "solid", "verdict was %s" % lines[-1]["done"]["verdict"])

print("== too few lines to go on")
status, kind, lines = sync(video=video, subtitle=write_srt(starts[:5], length))
check(status == 200 and lines[-1].get("subtitle") is None and "done" in lines[-1], "done with no subtitle")

print("== a file that is not a video")
status, kind, lines = sync(video=seen + "/starts.json", subtitle=late)
check(status == 200 and list(lines[-1]) == ["error"] and "lapse exited with 1" in lines[-1]["error"],
      "ends in an error: %s" % lines[-1])

print("== things that are not right")
status, kind, answer = sync(video=seen + "/nope.mkv", subtitle=late)
check(status == 404 and answer == {"error": seen + "/nope.mkv is not in the lapse container. "
                                    "Mount your media at the same path as in Bazarr."}, "404 for a missing video")
for what, fields in [
    ("no subtitle", {"video": video}),
    ("not base64", {"video": video, "subtitle": "this is not base64!"}),
    ("a format it does not read", {"video": video, "subtitle": late, "filename": "movie.txt"}),
    ("no video and no reference", {"subtitle": late}),
    ("a reference without a name", {"subtitle": late, "reference": late}),
    ("a video that is not a path", {"video": 7, "subtitle": late}),
    ("a negative track", {"video": video, "subtitle": late, "audio_track": -1}),
    ("a track that is text", {"video": video, "subtitle": late, "sub_track": "1"}),
]:
    status, kind, answer = sync(**fields)
    check(status == 400 and answer.get("error"), "400 for " + what)

conn = http.client.HTTPConnection(url.hostname, url.port, timeout=30)
conn.request("POST", "/api/sync", "this is not json", {"Content-Type": "application/json"})
answer = conn.getresponse()
check(answer.status == 400 and "error" in json.loads(answer.read()), "400 for a body that is not json")
conn.close()

# says it is sending 30 MB and then does not, so the server has to answer without reading
sock = socket.create_connection((url.hostname, url.port), timeout=30)
sock.sendall(b"POST /api/sync HTTP/1.1\r\nHost: x\r\nContent-Length: 30000000\r\n\r\n")
check(sock.recv(4096).startswith(b"HTTP/1.1 400"), "400 for a body over the limit")
sock.close()

print("== the client hangs up")
long_video = seen + "/long.mkv"
long_body = json.dumps({"video": long_video, "subtitle": base64.b64encode(late).decode(), "filename": "movie.srt"})


def hang_up(conn, answer):
    answer.close()    # the response keeps the socket open until it is closed too
    conn.close()


def start_long():
    conn = http.client.HTTPConnection(url.hostname, url.port, timeout=60)
    conn.request("POST", "/api/sync", long_body)
    return conn, conn.getresponse()


# a whole sync of this takes ten seconds or more, so a process gone this fast was killed
conn, answer = start_long()
check(answer.status == 200 and lapse_running() == 1, "lapse is running for the request")
hang_up(conn, answer)
check(gone_within(2), "lapse was killed when the client left straight away")

# the sync goes quiet for a few seconds after the first stretch of progress
conn, answer = start_long()
line = b"{"
while line.startswith(b"{"):
    line = answer.readline()
check(line == b"\n", "an empty line came while lapse was quiet")
check(lapse_running() == 1, "lapse is still running during the quiet")
hang_up(conn, answer)
check(gone_within(2), "lapse was killed when the client left in a quiet phase")

conn, answer = start_long()
line = b"\n"
while line == b"\n":
    line = answer.readline()
check("progress" in json.loads(line) and lapse_running() == 1, "lapse is still running after a progress line")
hang_up(conn, answer)
check(gone_within(2), "lapse was killed when the client left part way through")

print("== lines are not held back")
try:
    import requests
except ImportError:
    requests = None
    print("skipped, python requests is not here")
if requests:
    # Bazarr reads it like this, and iter_lines waits for 512 bytes on a stream that is not chunked
    began = time.time()
    first = None
    with requests.post(url.geturl() + "/api/sync", data=long_body, stream=True, timeout=60) as answer:
        for line in answer.iter_lines():
            if line:
                first = time.time() - began
                break
    check(first is not None and first < 2, "the first progress line came after %s seconds" % (first and round(first, 1)))
    check(gone_within(2), "lapse was killed when requests closed the answer")

print("== four at once")
seen_running = []
stop = threading.Event()


def watch():
    while not stop.is_set():
        seen_running.append(lapse_running())
        time.sleep(0.1)


threading.Thread(target=watch).start()
shifts = [1500, 3000, 4200, 6000]
with concurrent.futures.ThreadPoolExecutor(4) as pool:
    jobs = [pool.submit(sync, video=seen + "/copy%d.mkv" % [1, 2, 3, 4][i],
                        subtitle=write_srt([s + shift for s in starts], length))
            for i, shift in enumerate(shifts)]
    time.sleep(0.5)
    check(request("GET", "/api/version")[0] == 200, "the server answers while they run")
    answers = [job.result() for job in jobs]
stop.set()
check(max(seen_running) >= 2, "more than one lapse at a time (%d)" % max(seen_running))
for shift, (status, kind, lines) in zip(shifts, answers):
    done = lines[-1].get("done", {})
    check(status == 200 and lines[-1].get("subtitle") and abs(done.get("offset_ms", 0) + shift) <= TOLERANCE,
          "the one that was %d ms late came back right" % shift)

check(lapse_running() == 0, "nothing left running")

if failures:
    print("\n%d failed" % len(failures))
    sys.exit(1)
print("\nall good")
