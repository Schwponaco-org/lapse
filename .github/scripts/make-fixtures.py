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

# Drops the same spoken line at times we pick ourselves and writes the subtitle
# that matches. The gaps are random but seeded, so the audio never repeats and
# a wrong offset cannot score as well as the right one.

import os
import random
import struct
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
# the last five minutes sit further out than the auto refinement looks around the offset
write_srt("concat_long.srt", [(a + 52000, b + 52000) if a >= 19 * 60 * 1000 else (a, b) for a, b in jcues])
# two per episode subtitles stuck together, the second one starting over at zero
write_srt("concat_ep.srt", [(a - EP, b - EP) if a >= EP else (a, b) for a, b in jcues])
# a scene taken out after eight minutes and another after sixteen, so three parts
write_srt("concat_three.srt", [(a + (30000 if a >= 8 * 60 * 1000 else 0) + (20000 if a >= 16 * 60 * 1000 else 0),
                                b + (30000 if a >= 8 * 60 * 1000 else 0) + (20000 if a >= 16 * 60 * 1000 else 0)) for a, b in jcues])
# the last three minutes sit twenty minutes out, too short a part for two slices to find
write_srt("concat_far.srt", [(a + 20 * 60 * 1000, b + 20 * 60 * 1000) if a >= 21 * 60 * 1000 else (a, b) for a, b in jcues])

# timed for 24 fps and played at 25, with a cut on top of the drift
RATIO = 25 / 24
write_srt("drift_cut.srt", [(round(a * RATIO) + (4000 if a >= EP else 0), round(b * RATIO) + (4000 if a >= EP else 0)) for a, b in jcues])
# the same drift, with the last five minutes sitting further out than the refinement looks
write_srt("drift_long.srt", [(round(a * RATIO) + (52000 if a >= 19 * 60 * 1000 else 0),
                              round(b * RATIO) + (52000 if a >= 19 * 60 * 1000 else 0)) for a, b in jcues])

# cues that match nothing in either video, so there is no right answer to find
random.seed(99)
bogus = []
t = 1000
while t < 140 * 1000:
    bogus.append((t, t + 900))
    t += random.randint(1100, 2600)
write_srt("unrelated.srt", bogus)

# cues six seconds apart that only their lengths tell apart, so a VobSub that
# loses the time a picture leaves the screen has nothing to line up by. The
# .sub is under 256KB, which is where every picture but the first lost it
def picture(length):
    spu = struct.pack(">HH", 35, 20) + bytes(16) + struct.pack(">HH", 0, 29) + b"\x01\x03\x00\x00\xff" \
        + struct.pack(">HH", length * 90 // 1024, 29) + b"\x02\xff"
    pes = b"\x00\x00\x01\xbd" + struct.pack(">H", 9 + len(spu)) + b"\x81\x80\x05" + bytes(5) + b"\x20" + spu
    pack = b"\x00\x00\x01\xba\x44\x00\x04\x00\x04\x01\x01\x89\xc3\xf8" + pes
    return pack + b"\xff" * (2048 - len(pack))


steady = [(5000 + i * 6000, 5000 + i * 6000 + random.randint(800, 4500)) for i in range(40)]
write_srt("steady.srt", steady)
write_srt("steady_part.srt", steady[10:30])
idx = "# VobSub index file, v7 (do not modify this line!)\nsize: 720x576\nid: en, index: 0\n"
sub = b""
for a, b in steady[10:30]:
    idx += "timestamp: %s, filepos: %09x\n" % (ts(a + SHIFT).replace(",", ":"), len(sub))
    sub += picture(b - a)
open(os.path.join(work, "vobsub.idx"), "w").write(idx)
open(os.path.join(work, "vobsub.sub"), "wb").write(sub)

print("fixtures: %d cues, %d joined cues" % (len(cues), len(jcues)))
