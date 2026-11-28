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

import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

SEASON = "/media/TV/Show/Season 01/"
ONE = SEASON + "Show.S01E01.en.srt"
TWO = SEASON + "Show.S01E02.da.srt"
TOLERANCE = 250

failures = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures.append(what)


def api(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request("http://127.0.0.1:8080" + path, data, {"Content-Type": "application/json"})
    try:
        with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request, timeout=600) as answer:
            return json.load(answer)
    except urllib.error.HTTPError as e:
        return json.load(e)


def jobs():
    return {row["srt_path"]: row for row in api("/api/state")["jobs"]}


def wait_for(path, statuses, seconds=240):
    until = time.time() + seconds
    while time.time() < until:
        row = jobs().get(path)
        if row and row["status"] in statuses:
            return row
        time.sleep(3)
    return jobs().get(path)


def read(path):
    with open(path, encoding="utf-8") as file:
        return file.read()


def write(path, text):
    with open(path, "w", encoding="utf-8") as file:
        file.write(text)
    settled = time.time() - 600
    os.utime(path, (settled, settled))


def first_start(text):
    h, m, s, ms = (int(x) for x in re.search(r"(\d+):(\d+):(\d+),(\d+) -->", text).groups())
    return ((h * 60 + m) * 60 + s) * 1000 + ms


def moved(text, by):
    def stamp(found):
        h, m, s, ms = (int(x) for x in found.groups())
        at = ((h * 60 + m) * 60 + s) * 1000 + ms + by
        return "%02d:%02d:%02d,%03d" % (at // 3600000, at // 60000 % 60, at // 1000 % 60, at % 1000)
    return re.sub(r"(\d+):(\d+):(\d+),(\d+)", stamp, text)


stage = sys.argv[1]

if stage == "reference":
    print("== every other track of one episode against a reference")
    api("/api/pause", {"paused": True})
    right = read(ONE)
    reference = SEASON + "Show.S01E01.reference.srt"
    write(reference, right)
    late = moved(right, 3000)
    write(ONE, late)
    write(TWO, late)
    answer = api("/api/reference", {"ids": [jobs()[ONE]["id"]], "reference": reference,
                                    "everything": True, "output": "overwrite_backup"})
    check("error" not in answer, "the reference sync went through: %s" % answer.get("error"))
    check(abs(first_start(read(ONE)) - first_start(right)) <= TOLERANCE, "the episode's own track was moved onto it")
    check(read(TWO) == late, "the other episode in the folder was left alone")
    os.remove(reference)
    write(TWO, right)
    api("/api/pause", {"paused": False})

elif stage == "engine":
    print("== a setting changed in the web interface")
    form = dict(api("/api/state")["engine"])
    form["snap"] = "40"
    engine = api("/api/engine", {"engine": form})["engine"]
    check(engine["snap"] == "40", "saved snap 40")

elif stage == "backup":
    print("== a subtitle put in after an earlier sync, with MIN_CONFIDENCE and MEDIA_ROOT=/media/")
    answer = api("/api/reference", {"ids": [], "reference": ONE, "everything": False, "output": "overwrite_backup"})
    check("error" not in answer, "a reference under /media counts as in the library: %s" % answer.get("error"))
    old = read(TWO + ".bak")
    newer = moved(old, -1200)
    write(TWO, newer)
    row = wait_for(TWO, ("lowconf",))
    check(row and row["status"] == "lowconf", "the result under MIN_CONFIDENCE was marked unsure: %s" % (row and row["status"]))
    check(read(TWO) == newer, "the newer subtitle was put back, not the old .bak")
    check(read(TWO + ".bak") == old, "the old .bak is still there")

elif stage == "suffix":
    print("== OUTPUT_SUFFIX and MIN_CONFIDENCE on a subtitle that already fits")
    right = read(ONE)
    api("/api/resync", {"ids": [jobs()[ONE]["id"]]})
    row = wait_for(ONE, ("done", "lowconf", "failed"))
    synced = SEASON + "Show.S01E01.en.synced.srt"
    check(row and row["status"] == "lowconf" and not os.path.exists(synced),
          "the copy under MIN_CONFIDENCE was thrown away: %s" % (row and row["status"]))
    check(read(ONE) == right, "the original was not touched")

    print("== the web interface works on the synced copy")
    write(synced, moved(right, 1000))
    row = jobs()[ONE]
    answer = api("/api/preview", {"id": row["id"]})
    check(answer.get("path") == synced, "the preview shows the synced copy: %s" % answer.get("path"))
    answer = api("/api/shift", {"ids": [row["id"]], "ms": -1000, "output": "overwrite"})
    check("error" not in answer, "the shift went through: %s" % answer.get("error"))
    check(abs(first_start(read(synced)) - first_start(right)) <= 1, "the synced copy was moved")
    check(read(ONE) == right, "the original was left alone")
    answer = api("/api/convert", {"ids": [row["id"]], "format": ".vtt", "drop": True})
    check("error" not in answer, "the convert went through: %s" % answer.get("error"))
    check(os.path.exists(SEASON + "Show.S01E01.en.synced.vtt") and not os.path.exists(synced),
          "the synced copy became a .vtt")
    check(ONE in jobs(), "the original is still on record")
    os.remove(SEASON + "Show.S01E01.en.synced.vtt")

if failures:
    print("\n%d failed" % len(failures))
    sys.exit(1)
