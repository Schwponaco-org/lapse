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

import os
import sqlite3
import sys

SHIFT = -4200
TOLERANCE = 250

expected = {
    "/media/Movies/Test Movie (2024)/Test Movie (2024).en.srt":
        "/media/Movies/Test Movie (2024)/Test Movie (2024).mkv",
    "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Subs/2_English.srt":
        "/media/Movies/Scene.Release.2024.1080p.BluRay.x264-GRP/Scene.Release.2024.1080p.BluRay.x264-GRP.mkv",
    "/media/TV/Show/Season 01/Show.S01E01.en.srt":
        "/media/TV/Show/Season 01/Show.S01E01.1080p.WEB-DL.mkv",
    "/media/TV/Show/Season 01/Show.S01E02.da.srt":
        "/media/TV/Show/Season 01/Show.S01E02.1080p.WEB-DL.mkv",
}

if len(sys.argv) > 1 and sys.argv[1] == "extra":
    expected["/media/TV/Show/Season 01/Subs/Show.S01E03.english.srt"] = \
        "/media/TV/Show/Season 01/Show.S01E03.1080p.WEB-DL.mkv"

rows = {}
for sub, video, offset, confidence, status, backup in sqlite3.connect("/data/lapse.db").execute(
        "SELECT srt_path, video_path, offset_ms, confidence, status, backup_path FROM sync_jobs"):
    rows[sub] = (video, offset, confidence, status, backup)

failures = []

for sub, video in expected.items():
    if sub not in rows:
        failures.append("never synced: %s" % sub)
        continue
    got_video, offset, confidence, status, backup = rows[sub]
    if got_video != video:
        failures.append("%s was matched to the wrong video: %s" % (sub, got_video))
    if status != "done":
        failures.append("%s came back as %s" % (sub, status))
    if offset is None or abs(offset - SHIFT) > TOLERANCE:
        failures.append("%s got offset %s, expected around %d" % (sub, offset, SHIFT))
    if confidence is None or confidence < 0.8:
        failures.append("%s only scored %s" % (sub, confidence))
    if not backup or not os.path.exists(backup):
        failures.append("%s has no backup on disk" % sub)

for sub in rows:
    if sub not in expected:
        failures.append("something got synced that should have been left alone: %s" % sub)

if failures:
    for line in failures:
        print("FAIL:", line)
    sys.exit(1)

print("database looks right, %d pairs synced" % len(rows))
