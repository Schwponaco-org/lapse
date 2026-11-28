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

import codecs
import os
import re
import shutil

READABLE = {".srt", ".vtt", ".ass", ".ssa"}

CLOCK = re.compile(r"(?:(\d+):)?(\d{1,2}):(\d{2})[.,](\d{1,3})")


def kind(path):
    ext = os.path.splitext(path)[1].lower()
    if ext not in READABLE:
        raise RuntimeError("Cannot read that format: " + os.path.basename(path))
    return ext


WESTERN = ("æøåé", "åäöé", "äöüß", "éèàçêâîôûëïùœ", "áéíóúñü", "àèéìòù", "ãõáéíóúâêôçà", "ëïéèöó")
CENTRAL = ("ąćęłńóśźż", "áčďéěíňóřšťúůýž", "áäčďéíľňóôšťúýž", "áéíóöőúüű", "čćđšž", "ăâîşţ")

CODEPAGES = {"cp1252": WESTERN, "cp1250": CENTRAL, "iso-8859-2": CENTRAL, "cp1251": (None,)}


def letter(c):
    if c.isascii():
        return c.isalpha()
    return ("\xc0" <= c <= "\u024f" and c not in "×÷") or "\u0400" <= c <= "\u04ff"


def fit(text, language):
    total = 0
    for i, c in enumerate(text):
        if c < "\x80":
            continue
        before = i > 0 and letter(text[i - 1])
        after = i + 1 < len(text) and letter(text[i + 1])
        if not letter(c):
            if c < "\xa0" or (before and after):
                total -= 5
            elif c in "¿¡" and after:
                total += 3
        elif not language:
            latin = (before and text[i - 1] < "\x80") or (after and text[i + 1] < "\x80")
            word = (before and text[i - 1] >= "\u0400") or (after and text[i + 1] >= "\u0400")
            if latin or c < "\u0400":
                total -= 1
            elif word:
                total += 3
        elif before and after and text[i - 1] >= "\x80" and text[i + 1] >= "\x80":
            total -= 1
        else:
            total += 3 if c.lower() in language else -1
    return total


def codepage(raw):
    best, most = "cp1252", None
    for page, languages in CODEPAGES.items():
        try:
            text = raw.decode(page)
        except UnicodeDecodeError:
            continue
        points = max(fit(text, language) for language in languages)
        if most is None or points > most:
            best, most = page, points
    return best


def utf16(raw):
    look = raw[:4096]
    if len(look) < 16:
        return None
    total = len(look) // 2
    even = look[0::2].count(0)
    odd = look[1::2].count(0)
    if odd > total * 3 // 10 and even < total // 20:
        return "utf-16-le"
    if even > total * 3 // 10 and odd < total // 20:
        return "utf-16-be"
    return None


def charset(path):
    with open(path, "rb") as file:
        raw = file.read()
    if raw.startswith(codecs.BOM_UTF8):
        return "utf-8-sig"
    if raw.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
        return "utf-16"
    found = utf16(raw)
    if found:
        return found
    try:
        raw.decode("utf-8")
        return "utf-8"
    except UnicodeDecodeError:
        return codepage(raw)


def read(path):
    with open(path, "r", encoding=charset(path), errors="replace", newline="") as file:
        return file.readlines()


def to_ms(text):
    found = CLOCK.match(text.strip())
    if not found:
        return None
    hours, minutes, seconds, rest = found.groups()
    return (int(hours or 0) * 3600 + int(minutes) * 60 + int(seconds)) * 1000 \
        + int(rest.ljust(3, "0"))


def from_ms(ms, dot):
    ms = max(0, ms)
    hours, ms = divmod(ms, 3600000)
    minutes, ms = divmod(ms, 60000)
    seconds, ms = divmod(ms, 1000)
    return "%02d:%02d:%02d%s%03d" % (hours, minutes, seconds, "." if dot else ",", ms)


def from_ms_ass(ms):
    ms = (max(0, ms) + 5) // 10 * 10
    hours, ms = divmod(ms, 3600000)
    minutes, ms = divmod(ms, 60000)
    seconds, ms = divmod(ms, 1000)
    return "%d:%02d:%02d.%02d" % (hours, minutes, seconds, ms // 10)


def dialogue(line):
    if not line.startswith("Dialogue:"):
        return None
    fields = line.split(":", 1)[1].split(",")
    return fields if len(fields) > 9 else None


def preview(path, limit=6):
    lines = read(path)
    ass = kind(path) in (".ass", ".ssa")
    cues = []

    for number, line in enumerate(lines):
        if len(cues) == limit:
            break
        if ass:
            fields = dialogue(line)
            start = to_ms(fields[1]) if fields else None
            text = ",".join(fields[9:]).strip() if fields else ""
        elif "-->" in line:
            start = to_ms(line.split("-->")[0])
            text = " ".join(after.strip() for after in lines[number + 1:number + 3]).strip()
        else:
            continue
        if start is not None:
            cues.append({"ms": start, "text": re.sub(r"<[^>]+>|\{[^}]*\}", "", text)[:80]})

    if not cues:
        raise RuntimeError("Found no timings in " + os.path.basename(path))
    return cues


def text_cues(lines):
    cues = []
    for number, line in enumerate(lines):
        if "-->" not in line:
            continue
        left, right = line.split("-->", 1)
        start, end = to_ms(left), to_ms(right.strip())
        if start is None or end is None:
            continue
        body = []
        for after in lines[number + 1:]:
            if not after.strip():
                break
            body.append(after.rstrip("\r\n"))
        cues.append((start, end, body))
    return cues


def ass_cues(lines):
    cues = []
    for line in lines:
        fields = dialogue(line)
        if not fields:
            continue
        start, end = to_ms(fields[1]), to_ms(fields[2])
        if start is None or end is None:
            continue
        text = re.sub(r"\{[^}]*\}", "", ",".join(fields[9:]).rstrip("\r\n"))
        cues.append((start, end, [part for part in re.split(r"\\[Nn]", text) if part]))
    return cues


def cues_of(path):
    lines = read(path)
    if kind(path) in (".ass", ".ssa"):
        cues = ass_cues(lines)
    else:
        cues = text_cues(lines)
    if not cues:
        raise RuntimeError("Found no cues in " + os.path.basename(path))
    return cues


def as_srt(cues):
    out = []
    number = 1
    for start, end, body in cues:
        out.append("%d\n" % number)
        out.append("%s --> %s\n" % (from_ms(start, False), from_ms(end, False)))
        for line in body:
            out.append(line + "\n")
        out.append("\n")
        number = number + 1
    return out


def as_vtt(cues):
    out = ["WEBVTT\n", "\n"]
    for start, end, body in cues:
        out.append("%s --> %s\n" % (from_ms(start, True), from_ms(end, True)))
        for line in body:
            out.append(line + "\n")
        out.append("\n")
    return out


ASS_HEAD = """[Script Info]
ScriptType: v4.00+
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""

SSA_HEAD = """[Script Info]
ScriptType: v4.00

[V4 Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding
Style: Default,Arial,20,16777215,255,0,0,0,0,1,2,0,2,10,10,10,0,1

[Events]
Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def as_ass(cues):
    out = [ASS_HEAD]
    for start, end, body in cues:
        out.append("Dialogue: 0,%s,%s,Default,,0,0,0,,%s\n"
                   % (from_ms_ass(start), from_ms_ass(end), "\\N".join(body)))
    return out


def as_ssa(cues):
    out = [SSA_HEAD]
    for start, end, body in cues:
        out.append("Dialogue: Marked=0,%s,%s,Default,,0,0,0,,%s\n"
                   % (from_ms_ass(start), from_ms_ass(end), "\\N".join(body)))
    return out


WRITERS = {".srt": as_srt, ".vtt": as_vtt, ".ass": as_ass, ".ssa": as_ssa}


def convert(path, want, drop):
    if want not in WRITERS:
        raise RuntimeError("Cannot write that format: " + str(want))

    target = os.path.splitext(path)[0] + want
    if target == path or (os.path.exists(target) and os.path.samefile(target, path)):
        raise RuntimeError(os.path.basename(path) + " is " + want + " already")

    cues = cues_of(path)
    if os.path.exists(target) and not os.path.exists(target + ".bak"):
        shutil.copy2(target, target + ".bak")

    with open(target + ".part", "w", encoding="utf-8") as file:
        file.writelines(WRITERS[want](cues))
    os.replace(target + ".part", target)

    if drop:
        os.remove(path)
    return target
