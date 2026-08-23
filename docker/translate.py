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
import urllib.error
import urllib.parse
import urllib.request

PROVIDER  = os.environ.get("TRANSLATE_PROVIDER", "deepl").strip().lower()
KEY       = os.environ.get("TRANSLATE_KEY", "").strip()
URL       = os.environ.get("TRANSLATE_URL", "").strip().rstrip("/")
SOURCE    = os.environ.get("TRANSLATE_FROM", "").strip()
TARGETS   = [t.strip() for t in os.environ.get("TRANSLATE_TO", "").split(",") if t.strip()]
BATCH     = int(os.environ.get("TRANSLATE_BATCH", "40"))

READABLE = {".srt", ".vtt", ".ass", ".ssa"}

TIMING = re.compile(r"^\s*(\d{1,3}:)?\d{1,2}:\d{2}[.,]\d{1,3}\s*-->")
COUNTER = re.compile(r"^\s*\d+\s*$")
DIALOGUE = re.compile(r"^(Dialogue\s*:(?:[^,]*,){9})(.*)$")
LEADING = re.compile(r"^(?:\{[^}]*\})+")


def ready():
    if PROVIDER == "libretranslate":
        return bool(URL)
    return bool(KEY)


def named(path, language):
    base, ext = os.path.splitext(path)
    stem, tail = os.path.splitext(base)
    if tail and 2 <= len(tail) - 1 <= 3:
        base = stem
    return "%s.%s%s" % (base, language, ext)


def pull_lines(lines, dialogue_only):
    wanted = []
    for number, line in enumerate(lines):
        stripped = line.strip()

        if dialogue_only:
            match = DIALOGUE.match(stripped)
            if match and match.group(2).strip():
                wanted.append((number, match.group(2)))
            continue

        if not stripped or TIMING.search(line) or COUNTER.match(line):
            continue
        if stripped.startswith(("WEBVTT", "NOTE", "STYLE", "REGION")):
            continue
        wanted.append((number, line.rstrip("\r\n")))
    return wanted


def put_lines(lines, wanted, translated):
    out = list(lines)
    for (number, original), replacement in zip(wanted, translated):
        line = lines[number]
        ending = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
        match = DIALOGUE.match(line.strip())
        if match:
            out[number] = match.group(1) + replacement + ending
        else:
            out[number] = replacement + ending
    return out


def keep_markup(original, replacement):
    tags = LEADING.match(original)
    if not tags or replacement.startswith("{"):
        return replacement
    return tags.group(0) + replacement


def deepl(texts, language):
    host = "https://api-free.deepl.com" if KEY.endswith(":fx") else "https://api.deepl.com"
    body = {"text": texts, "target_lang": language.upper(), "tag_handling": "xml"}
    if SOURCE:
        body["source_lang"] = SOURCE.upper()
    answer = ask(URL or host, "/v2/translate", body, {"Authorization": "DeepL-Auth-Key " + KEY})
    return [item["text"] for item in answer["translations"]]


def google(texts, language):
    body = {"q": texts, "target": language, "format": "text"}
    if SOURCE:
        body["source"] = SOURCE
    where = "/language/translate/v2?key=" + urllib.parse.quote(KEY)
    answer = ask(URL or "https://translation.googleapis.com", where, body, {})
    return [item["translatedText"] for item in answer["data"]["translations"]]


def libretranslate(texts, language):
    body = {"q": texts, "source": SOURCE or "auto", "target": language, "format": "text"}
    if KEY:
        body["api_key"] = KEY
    answer = ask(URL, "/translate", body, {})
    got = answer["translatedText"]
    return got if isinstance(got, list) else [got]


PROVIDERS = {"deepl": deepl, "google": google, "libretranslate": libretranslate}


def ask(host, where, body, headers):
    headers = dict(headers)
    headers["Content-Type"] = "application/json"
    request = urllib.request.Request(host + where, json.dumps(body).encode(), headers)
    try:
        with urllib.request.urlopen(request, timeout=120) as answer:
            return json.loads(answer.read())
    except urllib.error.HTTPError as e:
        raise RuntimeError("%s said %d: %s" % (PROVIDER, e.code, e.read().decode("utf-8", "replace")[:300]))


def translate(texts, language):
    provider = PROVIDERS.get(PROVIDER)
    if not provider:
        raise RuntimeError("No such provider: " + PROVIDER)

    done = []
    for start in range(0, len(texts), BATCH):
        batch = texts[start:start + BATCH]
        got = provider(batch, language)
        if len(got) != len(batch):
            raise RuntimeError("%s gave back %d lines for %d" % (PROVIDER, len(got), len(batch)))
        done += got
    return done


def remember(conn, source, output, language, status, detail):
    conn.execute(
        """
        INSERT OR REPLACE INTO translations
        (source_path, output_path, language, provider, status, detail)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (source, output, language, PROVIDER, status, detail)
    )
    conn.commit()


def work(conn, path, language):
    try:
        output, detail = run(path, language)
        remember(conn, path, output, language, "done", detail)
        return True
    except Exception as e:
        remember(conn, path, named(path, language), language, "failed", str(e)[:400])
        print("Could not translate", path, "to", language, "->", e)
        return False


def run(path, language):
    if not ready():
        raise RuntimeError("No translation key or address is set")

    ext = os.path.splitext(path)[1].lower()
    if ext not in READABLE:
        raise RuntimeError("Cannot translate that format")

    output = named(path, language)
    if os.path.exists(output):
        return output, "already there"

    with open(path, "r", encoding="utf-8", errors="replace") as file:
        lines = file.readlines()

    wanted = pull_lines(lines, ext in (".ass", ".ssa"))
    if not wanted:
        raise RuntimeError("Found no text to translate")

    got = translate([text for _, text in wanted], language)
    got = [keep_markup(original, new) for (_, original), new in zip(wanted, got)]

    temporary = output + ".part"
    with open(temporary, "w", encoding="utf-8") as file:
        file.writelines(put_lines(lines, wanted, got))
    os.replace(temporary, output)

    return output, "%d lines" % len(wanted)
