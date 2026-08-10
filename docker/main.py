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
import queue
import shutil
import signal
import sqlite3
import subprocess
import time
from watchdog.observers import Observer
from watchdog.observers.polling import PollingObserver
from watchdog.events import FileSystemEventHandler

DB_PATH        = os.environ.get("DB_PATH", "lapse.db")
MEDIA_ROOT     = os.environ.get("MEDIA_ROOT", "/media")
LAPSE          = os.environ.get("LAPSE_BIN", "./lapse")
MODE           = os.environ.get("MODE", "auto")
PENALTY        = os.environ.get("PENALTY", "6")
SCAN_INTERVAL  = int(os.environ.get("SCAN_INTERVAL", "900"))
MIN_CONFIDENCE = float(os.environ.get("MIN_CONFIDENCE", "0"))
MAX_ATTEMPTS   = int(os.environ.get("MAX_ATTEMPTS", "3"))
TIMEOUT        = int(os.environ.get("TIMEOUT", "1800"))
POLLING        = os.environ.get("POLLING", "0") == "1"

MEDIA_ROOTS = [p.strip() for p in MEDIA_ROOT.split(",") if p.strip()]

VIDEO_EXTS = {
    ".mp4", ".mkv", ".avi", ".mov", ".m4v", ".ts", ".m2ts", ".mts", ".webm",
    ".wmv", ".mpg", ".mpeg", ".flv", ".ogv", ".ogm", ".divx", ".vob", ".rmvb", ".3gp",
}

# Everything we know of as a subtitle, not everything the engine can read yet.
# The engine tells us that part at startup and the rest is left alone
SUBTITLE_EXTS = {
    ".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx", ".sup", ".smi", ".sami",
    ".ttml", ".dfxp", ".sbv", ".mpl2", ".lrc", ".stl", ".scc", ".mcc", ".cap",
}

ENGINE_FORMATS = {".srt", ".ass", ".ssa", ".vtt"}

SKIP_DIRS = {"@eaDir", "#recycle", "#snapshot", "lost+found", ".Trash", "@Recycle"}

LANGUAGE_WORDS = {
    "en", "eng", "english", "da", "dan", "dansk", "danish", "sv", "swe", "swedish",
    "no", "nor", "nb", "norwegian", "fi", "fin", "finnish", "is", "isl", "icelandic",
    "de", "ger", "deu", "german", "fr", "fre", "fra", "french", "es", "spa", "spanish",
    "it", "ita", "italian", "nl", "dut", "nld", "dutch", "pt", "por", "portuguese",
    "pl", "pol", "polish", "ru", "rus", "russian", "cs", "ces", "czech", "tr", "tur",
    "turkish", "ar", "ara", "arabic", "zh", "chi", "chinese", "ja", "jpn", "japanese",
    "ko", "kor", "korean", "hu", "hun", "hungarian", "ro", "ron", "romanian", "el",
    "ell", "greek", "he", "heb", "hebrew", "hi", "hin", "hindi", "th", "tha", "thai",
    "forced", "sdh", "cc", "hearing", "impaired", "full", "default", "sub", "subs",
    "subtitle", "subtitles", "srt", "track",
}

jobs = queue.Queue()
running = True


def engine_formats():
    try:
        result = subprocess.run([LAPSE, "--formats"], capture_output=True, text=True, timeout=30)
        found = set()
        for word in result.stdout.split():
            if word.startswith("."):
                found.add(word.lower())
        if found:
            return found
        print("Engine did not answer --formats, going with the formats we know about")
    except Exception as e:
        print("Could not ask the engine which formats it reads:", e)
    return ENGINE_FORMATS


def init_db():
    directory = os.path.dirname(DB_PATH)
    if directory:
        os.makedirs(directory, exist_ok=True)

    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sync_jobs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            video_path TEXT NOT NULL,
            srt_path TEXT NOT NULL,
            backup_path TEXT,
            slope REAL,
            intercept_ms REAL,
            offset_ms REAL,
            confidence REAL,
            srt_mtime REAL,
            attempts INTEGER DEFAULT 0,
            status TEXT DEFAULT 'pending',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.execute("""
        CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_jobs_video_srt
        ON sync_jobs(video_path, srt_path)
    """)
    for name, kind in [("slope", "REAL"), ("intercept_ms", "REAL"), ("offset_ms", "REAL"),
                        ("confidence", "REAL"), ("srt_mtime", "REAL"), ("attempts", "INTEGER DEFAULT 0")]:
        try:
            conn.execute("ALTER TABLE sync_jobs ADD COLUMN %s %s" % (name, kind))
        except Exception:
            pass
    conn.commit()
    return conn


def normalize(name):
    for ch in "._-[]()+'":
        name = name.replace(ch, " ")
    return set(name.lower().split())


def subtitle_tokens(base):
    return normalize(base) - LANGUAGE_WORDS


def episode_tag(name):
    match = re.search(r"[sS](\d{1,2})[ ._-]?[eE](\d{1,3})", name)
    if match:
        return (int(match.group(1)), int(match.group(2)))
    match = re.search(r"(?<!\d)(\d{1,2})x(\d{2})(?!\d)", name)
    if match:
        return (int(match.group(1)), int(match.group(2)))
    return None


def match_video(sbase, videos):
    tokens = subtitle_tokens(sbase)
    stag = episode_tag(sbase)
    best, best_score = None, 0.0
    usable = []

    for vbase, vpath in videos:
        vtag = episode_tag(vbase)
        # Two different episodes in one folder share nearly every other word,
        # so a tag that disagrees settles it before we look at anything else
        if stag and vtag and stag != vtag:
            continue
        usable.append((vbase, vpath))

        vtokens = normalize(vbase)
        if not vtokens:
            continue
        score = len(vtokens & tokens) / len(vtokens)
        if score > best_score:
            best_score = score
            best = vpath

    if best and best_score >= 0.5:
        return best
    # Nothing lined up by name, but if only one video can belong to it anyway
    # then the subtitle is for that one
    if len(usable) == 1:
        return usable[0][1]
    return None


def scan(path):
    videos = {}
    subtitles = {}

    for root, dirs, files in os.walk(path):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(".")]

        for filename in files:
            if filename.startswith("."):
                continue
            base, ext = os.path.splitext(filename)
            ext = ext.lower()
            full_path = os.path.join(root, filename)

            if ext in VIDEO_EXTS:
                videos.setdefault(root, []).append((base, full_path))
            elif ext in SUBTITLE_EXTS:
                subtitles.setdefault(root, []).append((base, full_path))

    return videos, subtitles


def videos_in(directory):
    found = []
    try:
        for filename in sorted(os.listdir(directory)):
            if filename.startswith("."):
                continue
            base, ext = os.path.splitext(filename)
            if ext.lower() in VIDEO_EXTS:
                found.append((base, os.path.join(directory, filename)))
    except OSError:
        pass
    return found


def find_pairs(path, verbose=False):
    videos, subtitles = scan(path)
    pairs = []

    for directory in sorted(subtitles):
        candidates = videos.get(directory)
        # Scene releases drop the subtitles in a Subs folder beside the video
        if not candidates:
            candidates = videos_in(os.path.dirname(directory))

        for sbase, spath in subtitles[directory]:
            video = match_video(sbase, candidates) if candidates else None
            if video:
                pairs.append((video, spath))
            elif verbose:
                print("No video match for:", spath)

    return pairs


def wait_stable(path):
    # A file that is still being copied in gives us half a video, so leave it
    # alone until nothing has touched it for a while
    for _ in range(60):
        if not running:
            return False
        try:
            info = os.stat(path)
        except OSError:
            return False
        if info.st_size > 0 and time.time() - info.st_mtime > 30:
            return True
        time.sleep(2)
    return False


def file_mtime(path):
    try:
        return os.path.getmtime(path)
    except OSError:
        return None


def previous_job(conn, video_path, srt_path):
    cur = conn.execute(
        "SELECT status, attempts, srt_mtime FROM sync_jobs WHERE video_path = ? AND srt_path = ?",
        (video_path, srt_path)
    )
    return cur.fetchone()


def needs_work(row, mtime):
    if row is None:
        return True

    status, attempts, srt_mtime = row
    # Somebody replaced the subtitle since we last wrote it, so it is a new job
    if mtime is not None and srt_mtime is not None and abs(mtime - srt_mtime) > 1:
        return True
    if status in ("done", "lowconf"):
        return False
    return (attempts or 0) < MAX_ATTEMPTS


def parse_output(output):
    # the engine writes one json line on stdout, everything else goes to stderr
    for line in reversed(output.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except ValueError:
                pass
    return {}


def run_sync(video_path, srt_path):
    command = [LAPSE, video_path, srt_path, MODE, "--json"]
    if MODE == "split":
        command.insert(4, PENALTY)

    result = subprocess.run(command, capture_output=True, text=True, timeout=TIMEOUT)
    values = parse_output(result.stdout or "")

    # 2 is "nothing lined up", 3 is "wrote it next to the original instead"
    if result.returncode not in (0, 2, 3):
        message = (result.stderr or "").strip()
        raise RuntimeError(message or "lapse exited with %d" % result.returncode)

    if not values:
        raise RuntimeError((result.stderr or "").strip() or "lapse said nothing")

    print("%s: %s offset=%sms sigma=%.1f agree=%.2f reference=%s" % (
        os.path.basename(srt_path), values.get("verdict"), values.get("offset_ms"),
        values.get("sigma", 0), values.get("agreement", 0), values.get("reference")))
    return values


def save_result(conn, video_path, srt_path, backup_path, values, attempts, status):
    conn.execute(
        """
        INSERT OR REPLACE INTO sync_jobs
        (video_path, srt_path, backup_path, slope, intercept_ms, offset_ms, confidence, srt_mtime, attempts, status)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            video_path,
            srt_path,
            backup_path,
            values.get("ratio", 1.0) - 1.0 if values.get("ratio") is not None else None,
            values.get("offset_ms"),
            values.get("offset_ms"),
            values.get("confidence"),
            file_mtime(srt_path),
            attempts,
            status,
        )
    )
    conn.commit()


def process(conn, video_path, srt_path):
    ext = os.path.splitext(srt_path)[1].lower()
    if ext not in ENGINE_FORMATS:
        return "unsupported"

    row = previous_job(conn, video_path, srt_path)
    if not needs_work(row, file_mtime(srt_path)):
        return "skipped"

    attempts = (row[1] or 0) + 1 if row else 1

    if not wait_stable(video_path) or not wait_stable(srt_path):
        print("Still being written, leaving it for the next scan:", srt_path)
        return "busy"

    print("Syncing:", srt_path)
    print("      to:", video_path)
    try:
        values = run_sync(video_path, srt_path)
    except Exception as e:
        print("Failed:", srt_path, "->", e)
        save_result(conn, video_path, srt_path, None, {}, attempts, "failed")
        return "failed"

    backup_path = srt_path + ".bak"
    if not os.path.exists(backup_path):
        backup_path = None

    status = "done"
    verdict = values.get("verdict")
    if not values.get("written"):
        print("Nothing lined up, left it alone:", srt_path)
        status = "lowconf"
    elif verdict == "unsure":
        # the engine wrote its guess beside the original rather than over it
        print("Not sure about this one, left the original and put the guess in",
              values.get("output"))
        status = "lowconf"

    confidence = values.get("confidence")
    if status == "done" and MIN_CONFIDENCE > 0 and confidence is not None \
            and confidence < MIN_CONFIDENCE and backup_path:
        shutil.copy2(backup_path, srt_path)
        print("Confidence %.2f is under %.2f, put the original back: %s" % (confidence, MIN_CONFIDENCE, srt_path))
        status = "lowconf"

    save_result(conn, video_path, srt_path, backup_path, values, attempts, status)
    return status


def run_scan(conn, path, verbose=False):
    if not os.path.isdir(path):
        return

    unsupported = 0
    for video, subtitle in find_pairs(path, verbose):
        if not running:
            return
        if process(conn, video, subtitle) == "unsupported":
            unsupported += 1

    if unsupported:
        print("Left", unsupported, "subtitles alone, the engine does not read those formats yet")


class MediaHandler(FileSystemEventHandler):
    def interesting(self, path):
        name = os.path.basename(path)
        if name.startswith("."):
            return False
        ext = os.path.splitext(name)[1].lower()
        return ext in VIDEO_EXTS or ext in SUBTITLE_EXTS

    def queue(self, path, is_directory):
        if is_directory:
            jobs.put(path)
        elif self.interesting(path):
            jobs.put(os.path.dirname(path))

    def on_created(self, event):
        self.queue(event.src_path, event.is_directory)

    def on_moved(self, event):
        self.queue(event.dest_path, event.is_directory)

    def on_closed(self, event):
        self.queue(event.src_path, event.is_directory)


def start_watching():
    handler = MediaHandler()
    observer = PollingObserver() if POLLING else Observer()
    watched = 0

    for root in MEDIA_ROOTS:
        if not os.path.isdir(root):
            print("Cannot watch, it is not mounted:", root)
            continue
        try:
            observer.schedule(handler, root, recursive=True)
            watched += 1
        except OSError as e:
            print("Cannot watch", root, "->", e)

    if not watched:
        print("Watching nothing, falling back to rescans only")
        return None

    observer.start()
    return observer


def settle(seconds):
    for _ in range(seconds):
        if not running:
            return
        time.sleep(1)


def take_pending():
    try:
        first = jobs.get(timeout=5)
    except queue.Empty:
        return set()
    if not running:
        return set()

    # A release lands as a pile of files, so let the rest of them turn up
    # before we go and look at the folder
    settle(10)
    directories = {first}
    while True:
        try:
            directories.add(jobs.get_nowait())
        except queue.Empty:
            break
    directories.discard(None)
    return directories


# The queue is what the loop sits and waits on, so put something in it to
# get out of that wait straight away instead of sitting out the timeout
def stop(signum, frame):
    global running
    running = False
    jobs.put(None)


def main():
    global ENGINE_FORMATS, SUBTITLE_EXTS

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    ENGINE_FORMATS = engine_formats()
    SUBTITLE_EXTS = SUBTITLE_EXTS | ENGINE_FORMATS
    print("Engine reads:", " ".join(sorted(ENGINE_FORMATS)))
    print("Library:", ", ".join(MEDIA_ROOTS))

    conn = init_db()
    observer = start_watching()

    for root in MEDIA_ROOTS:
        if not os.path.isdir(root):
            print("Not mounted, nothing to scan there:", root)
            continue
        run_scan(conn, root, verbose=True)
    last_scan = time.time()

    print("Watching for new files...")
    while running:
        for directory in take_pending():
            if not running:
                break
            run_scan(conn, directory, verbose=True)

        if running and SCAN_INTERVAL and time.time() - last_scan > SCAN_INTERVAL:
            for root in MEDIA_ROOTS:
                run_scan(conn, root)
            last_scan = time.time()

    print("Shutting down")
    if observer:
        observer.stop()
        observer.join()
    conn.close()


if __name__ == "__main__":
    main()
