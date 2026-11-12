#!/usr/bin/env python3
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
# Builds one sane subtitle in every format the engine reads, mutates each of
# them many times over and runs the engine on every mutant, looking for a
# crash or a sanitizer report. Meant for a binary built with -DLAPSE_SANITIZE=ON.
#
#   mutate-formats.py <lapse binary> <work dir> [rounds] [seed]
#
# Every mutant goes through --shift, which reads and writes the file without
# a reference, and one in eight is synced against a reference subtitle as
# well. A mutant that crashes is kept in <work dir>/crashes with the output.

import os
import random
import struct
import subprocess
import sys

lapse = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2])
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 60
seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
random.seed(seed)
os.makedirs(work, exist_ok=True)
os.makedirs(os.path.join(work, "crashes"), exist_ok=True)
os.chdir(work)

SHIFT = 4200
CUES = [(5000 + i * 7000, 8000 + i * 7000) for i in range(40)]


def ts(ms, sep=","):
    return "%02d:%02d:%02d%s%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, sep, ms % 1000)


def srt(cues):
    return "".join("%d\n%s --> %s\nline %d\n\n" % (i + 1, ts(a), ts(b), i) for i, (a, b) in enumerate(cues))


def vtt(cues):
    return "WEBVTT\n\n" + "".join("%s --> %s align:start\nline <00:00:00.500>%d\n\n" % (ts(a, "."), ts(b, "."), i) for i, (a, b) in enumerate(cues))


def ass_ts(ms):
    return "%d:%02d:%02d.%02d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000 // 10)


def ass(cues):
    head = ("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\nFormat: Name, Fontname\nStyle: Default,Arial\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")
    return head + "".join("Dialogue: 0,%s,%s,Default,,0,0,0,,line %d\\Nmore\n" % (ass_ts(a), ass_ts(b), i) for i, (a, b) in enumerate(cues))


def ssa(cues):
    head = ("[Script Info]\nScriptType: v4.00\n\n[Events]\n"
            "Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")
    return head + "".join("Dialogue: Marked=0,%s,%s,Default,,0,0,0,,line %d\n" % (ass_ts(a), ass_ts(b), i) for i, (a, b) in enumerate(cues))


def microdvd(cues):
    return "{1}{1}25.000\n" + "".join("{%d}{%d}line %d|two\n" % (round(a * 25 / 1000), round(b * 25 / 1000), i) for i, (a, b) in enumerate(cues))


def mpl2(cues):
    return "".join("[%d][%d]line %d|two\n" % (a // 100, b // 100, i) for i, (a, b) in enumerate(cues))


def subviewer(cues):
    head = "[INFORMATION]\n[TITLE]test\n[END INFORMATION]\n[SUBTITLE]\n[COLF]&HFFFFFF,[STYLE]bd,[SIZE]18,[FONT]Arial\n"
    return head + "".join("%s,%s\nline %d[br]two\n\n" % (ts(a, ".")[:-1], ts(b, ".")[:-1], i) for i, (a, b) in enumerate(cues))


def sbv(cues):
    return "".join("%s,%s\nline %d\n\n" % (ts(a, ".")[1:], ts(b, ".")[1:], i) for i, (a, b) in enumerate(cues))


def smi(cues):
    out = "<SAMI>\n<HEAD><TITLE>test</TITLE></HEAD>\n<BODY>\n"
    for i, (a, b) in enumerate(cues):
        out += "<SYNC Start=%d><P Class=ENCC>line %d<br>two</P>\n<SYNC Start=%d><P Class=ENCC>&nbsp;</P>\n" % (a, i, b)
    return out + "</BODY>\n</SAMI>\n"


def ttml(cues):
    out = ('<?xml version="1.0" encoding="UTF-8"?>\n<tt xmlns="http://www.w3.org/ns/ttml" xml:lang="en">\n'
           "<body><div>\n")
    for i, (a, b) in enumerate(cues):
        out += '<p begin="%s" end="%s" region="r1">line <span tts:fontStyle="italic">%d</span></p>\n' % (ts(a, "."), ts(b, "."), i)
    return out + "</div></body>\n</tt>\n"


def itt(cues):
    def tc(ms):
        return "%02d:%02d:%02d:%02d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000 // 40)
    out = ('<?xml version="1.0" encoding="UTF-8"?>\n<tt xmlns="http://www.w3.org/ns/ttml" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" '
           'ttp:timeBase="smpte" ttp:frameRate="25">\n<body><div>\n')
    for i, (a, b) in enumerate(cues):
        out += '<p begin="%s" end="%s">line %d</p>\n' % (tc(a), tc(b), i)
    return out + "</div></body>\n</tt>\n"


def scc(cues):
    def tc(ms):
        return "%02d:%02d:%02d:%02d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000 // 34)
    out = "Scenarist_SCC V1.0\n\n"
    for i, (a, b) in enumerate(cues):
        out += "%s\t9420 9420 94ae 94ae 9452 9452 97a2 97a2 c8e5 ecec ef80 942f 942f\n\n" % tc(a - 1000)
        out += "%s\t942c 942c\n\n" % tc(b)
    return out


def spruce(cues):
    def tc(ms):
        return "%02d:%02d:%02d:%02d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000 // 40)
    out = "$FontName = Arial\n$FontSize = 30\n//comment\n\n"
    for i, (a, b) in enumerate(cues):
        out += "%s , %s , line %d|two\n" % (tc(a), tc(b), i)
    return out


def ebu_stl(cues):
    def tc(ms):
        f = ms * 25 // 1000
        return bytes([f // 90000, f // 1500 % 60, f // 25 % 60, f % 25])
    gsi = bytearray(b" " * 1024)
    gsi[0:3] = b"850"
    gsi[3:6] = b"STL"
    gsi[6:8] = b"25"
    gsi[8:11] = b".01"
    gsi[256:264] = b"00000000"
    body = bytearray()
    for i, (a, b) in enumerate(cues):
        tti = bytearray(128)
        tti[0] = 0
        tti[1] = i & 0xff
        tti[2] = i >> 8
        tti[3] = 0xff
        tti[4] = 0
        tti[5:9] = tc(a)
        tti[9:13] = tc(b)
        tti[13] = 20
        tti[14] = 2
        tti[15] = 0
        text = ("line %d" % i).encode() + b"\x8a" + b"two"
        tti[16:16 + len(text)] = text
        for k in range(16 + len(text), 128):
            tti[k] = 0x8f
        body += tti
    return bytes(gsi) + bytes(body)


def pgs_segment(pts, kind, payload):
    return b"PG" + struct.pack(">II", pts, 0) + bytes([kind]) + struct.pack(">H", len(payload)) + payload


def sup(cues):
    out = bytearray()
    for i, (a, b) in enumerate(cues):
        pts = a * 90
        pcs = struct.pack(">HHBHBBBB", 1920, 1080, 0x10, i * 2, 0x80, 0, 0, 1) + struct.pack(">HBBHH", 0, 0, 0, 100, 900)
        out += pgs_segment(pts, 0x16, pcs)
        out += pgs_segment(pts, 0x17, struct.pack(">BBHHHH", 1, 0, 100, 900, 200, 50))
        out += pgs_segment(pts, 0x14, bytes([0, 0]) + bytes([1, 235, 128, 128, 255]))
        out += pgs_segment(pts, 0x15, struct.pack(">HBB", 0, 0, 0xc0) + struct.pack(">I", 7)[1:] + struct.pack(">HH", 4, 4) + bytes([0, 0]))
        out += pgs_segment(pts, 0x80, b"")
        pts = b * 90
        out += pgs_segment(pts, 0x16, struct.pack(">HHBHBBBB", 1920, 1080, 0x10, i * 2 + 1, 0x00, 0, 0, 0))
        out += pgs_segment(pts, 0x80, b"")
    return bytes(out)


def spu_packet(start_ms, stop_ms):
    # one SPU in one PES packet inside one pack, the way VobSub writes them
    pixels = b"\x00" * 16
    ctrl1 = struct.pack(">HH", 0, 0) + b"\x01" + b"\x03\x00\x00" + b"\xff"
    ctrl2_at = 4 + len(pixels) + len(ctrl1)
    date = (stop_ms - start_ms) * 90 // 1024
    ctrl2 = struct.pack(">HH", date, ctrl2_at) + b"\x02" + b"\xff"
    ctrl1 = struct.pack(">HH", 0, ctrl2_at) + ctrl1[4:]
    spu_body = pixels + ctrl1 + ctrl2
    spu = struct.pack(">HH", 4 + len(spu_body), 4 + len(pixels)) + spu_body
    pts = start_ms * 90
    pts_bytes = bytes([
        0x21 | ((pts >> 29) & 0x0e),
        (pts >> 22) & 0xff,
        0x01 | ((pts >> 14) & 0xfe),
        (pts >> 7) & 0xff,
        0x01 | ((pts << 1) & 0xfe),
    ])
    pes_body = b"\x81\x80\x05" + pts_bytes + b"\x20" + spu
    pes = b"\x00\x00\x01\xbd" + struct.pack(">H", len(pes_body)) + pes_body
    pack = b"\x00\x00\x01\xba" + b"\x44\x00\x04\x00\x04\x01\x01\x89\xc3\xf8"
    return pack + pes


def vobsub(cues):
    idx = "# VobSub index file, v7 (do not modify this line!)\nsize: 720x576\nid: en, index: 0\n"
    sub = bytearray()
    for a, b in cues:
        idx += "timestamp: %s, filepos: %09x\n" % (ts(a, ":"), len(sub))
        sub += spu_packet(a, b)
        while len(sub) % 2048:
            sub += b"\xff"
    return idx, bytes(sub)


def write(name, data):
    mode = "wb" if isinstance(data, (bytes, bytearray)) else "w"
    with open(name, mode) as f:
        f.write(data)


late = [(a + SHIFT, b + SHIFT) for a, b in CUES]
write("reference.srt", srt(CUES))

samples = {
    "late.srt": srt(late), "late.vtt": vtt(late), "late.ass": ass(late), "late.ssa": ssa(late),
    "micro.sub": microdvd(late), "mpl2.sub": mpl2(late), "viewer.sub": subviewer(late),
    "late.sbv": sbv(late), "late.smi": smi(late), "late.ttml": ttml(late), "late.dfxp": ttml(late),
    "late.itt": itt(late), "late.scc": scc(late), "spruce.stl": spruce(late), "ebu.stl": ebu_stl(late),
    "late.sup": sup(late), "late.idx": vobsub(late)[0],
}
for name, data in samples.items():
    write(name, data)
write("late.sub.vobsub", vobsub(late)[1])


def mutate(data):
    data = bytearray(data)
    n = len(data)
    for _ in range(random.randint(1, 4)):
        how = random.randint(0, 7)
        if n == 0:
            data = bytearray(random.randbytes(random.randint(1, 64)))
            n = len(data)
            continue
        if how == 0:
            for _ in range(random.randint(1, 16)):
                data[random.randrange(n)] = random.randrange(256)
        elif how == 1:
            data = data[:random.randrange(n)]
        elif how == 2:
            a = random.randrange(n)
            b = min(n, a + random.randint(1, 200))
            del data[a:b]
        elif how == 3:
            a = random.randrange(n)
            b = min(n, a + random.randint(1, 200))
            at = random.randrange(n)
            data[at:at] = data[a:b]
        elif how == 4:
            at = random.randrange(n)
            data[at:at] = random.randbytes(random.randint(1, 64))
        elif how == 5:
            a = random.randrange(n)
            b = min(n, a + random.randint(1, 32))
            data[a:b] = bytes([random.choice([0, 0xff, 0x7f, 0x80, ord("9"), ord(":"), ord("-")])]) * (b - a)
        elif how == 6:
            at = random.randrange(n)
            data[at:at + 1] = random.choice([b"99999999999", b"-1", b"::::", b",,,,", b"{}{}", b"[][]", b"<p", b"-->", b"\x00"])
        else:
            data = data + data[:random.randrange(n)]
        n = len(data)
    return bytes(data)


env = dict(os.environ)
env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=0:halt_on_error=1"
env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
env["LAPSE_CACHE"] = os.path.join(work, "cache")

crashes = 0
runs = 0


def run(args):
    global crashes, runs
    runs += 1
    try:
        got = subprocess.run([lapse] + args, capture_output=True, text=True, errors="replace", timeout=120, env=env)
    except subprocess.TimeoutExpired:
        return "timeout"
    text = got.stdout + got.stderr
    bad = got.returncode < 0 or got.returncode >= 128 or "AddressSanitizer" in text or "runtime error" in text or "LeakSanitizer" in text
    return text if bad else None


def report(label, path, text):
    global crashes
    crashes += 1
    keep = os.path.join(work, "crashes", "%03d-%s" % (crashes, os.path.basename(path)))
    os.replace(path, keep)
    with open(keep + ".log", "w") as f:
        f.write(label + "\n" + text)
    first = [l for l in text.splitlines() if "runtime error" in l or "ERROR: AddressSanitizer" in l or "Segmentation" in l]
    print("CRASH %s -> %s\n    %s" % (label, keep, first[0] if first else text.strip().splitlines()[-1:]))


for name, data in samples.items():
    stem, ext = os.path.splitext(name)
    for r in range(rounds):
        mutant = "mut-%s-%d%s" % (stem, r, ext)
        write(mutant, mutate(data if isinstance(data, (bytes, bytearray)) else data.encode()))
        if ext == ".idx":
            subname = mutant[:-4] + ".sub"
            if r % 2:
                write(subname, mutate(vobsub(late)[1]))
            else:
                write(subname, vobsub(late)[1])
        out = "out-" + mutant
        text = run(["--shift", "-4200", mutant, "--output", out, "--no-backup"])
        if text:
            report("shift " + mutant, mutant, text)
        elif r % 8 == 0:
            text = run(["reference.srt", mutant, "--force", "--no-backup", "--output", out])
            if text:
                report("sync " + mutant, mutant, text)
        for leftover in (mutant, out, out + ".tmp"):
            try:
                os.remove(leftover)
            except OSError:
                pass
        if ext == ".idx":
            try:
                os.remove(subname)
            except OSError:
                pass
    print("%-12s %d mutants" % (name, rounds))

print("%d runs, %d crashes" % (runs, crashes))
sys.exit(1 if crashes else 0)
