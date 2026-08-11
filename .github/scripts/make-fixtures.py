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

# Drops the same spoken line at times we pick ourselves and writes the subtitle
# that matches. The gaps are random but seeded, so the audio never repeats and
# a wrong offset cannot score as well as the right one.

import os
import random
import sys
import wave

work = sys.argv[1]
random.seed(7)

RATE = 16000
SHIFT = 4200
EARLY = -1500
GAP = 30 * 1000


def load_line():
    w = wave.open(os.path.join(work, "line.wav"))
    frames = w.readframes(w.getnframes())
    w.close()
    return frames


def write_wav(name, data, width=2):
    out = wave.open(os.path.join(work, name), "wb")
    out.setnchannels(1)
    out.setsampwidth(width)
    out.setframerate(RATE)
    out.writeframes(data)
    out.close()


def lay_out(total_ms, skip=None):
    line = load_line()
    line_ms = len(line) // 2 * 1000 // RATE
    buf = bytearray(total_ms * RATE // 1000 * 2)
    cues = []
    t = 3000
    while t + line_ms + 1000 < total_ms:
        if skip is None or not (skip - 8000 < t < skip + 3000):
            at = t * RATE // 1000 * 2
            buf[at:at + len(line)] = line
            cues.append((t, t + line_ms))
        t += random.randint(5000, 11000)
    return bytes(buf), cues


def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return "%02d:%02d:%02d,%03d" % (h, m, s, ms)


def write_srt(name, cues):
    out = []
    for i, (a, b) in enumerate(cues):
        out.append("%d\n%s --> %s\nline %d\n" % (i + 1, ts(max(a, 0)), ts(max(b, 0)), i + 1))
    open(os.path.join(work, name), "w").write("\n".join(out))


audio, cues = lay_out(150 * 1000)
write_wav("audio.wav", audio)

write_srt("truth.srt", cues)
write_srt("shifted.srt", [(a + SHIFT, b + SHIFT) for a, b in cues])
write_srt("early.srt", [(a + EARLY, b + EARLY) for a, b in cues])

# two parts joined into one video, with a quiet seam between them
EP = 12 * 60 * 1000
joined, jcues = lay_out(24 * 60 * 1000, skip=EP)
write_wav("concat_audio.wav", joined)

write_srt("concat_truth.srt", jcues)
# a segment the video does not have, so the second half sits later than the film
write_srt("concat_gap.srt", [(a + GAP, b + GAP) if a >= EP else (a, b) for a, b in jcues])
# two per episode subtitles stuck together, the second one starting over at zero
write_srt("concat_ep.srt", [(a - EP, b - EP) if a >= EP else (a, b) for a, b in jcues])

# cues that match nothing in either video, so there is no right answer to find
random.seed(99)
bogus = []
t = 1000
while t < 140 * 1000:
    bogus.append((t, t + 900))
    t += random.randint(1100, 2600)
write_srt("unrelated.srt", bogus)

print("fixtures: %d cues, %d joined cues" % (len(cues), len(jcues)))
