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

import json
import os
import sqlite3
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "index.html")

config = {}


def db():
    conn = sqlite3.connect(config["db_path"], timeout=30)
    conn.row_factory = sqlite3.Row
    return conn


def setting(conn, key, fallback):
    row = conn.execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
    return row[0] if row else fallback


def save_setting(conn, key, value):
    conn.execute("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)", (key, str(value)))
    conn.commit()


def folders():
    found = []
    for root in config["roots"]:
        found.append(root)
        try:
            names = sorted(os.listdir(root))
        except OSError:
            continue
        for name in names:
            path = os.path.join(root, name)
            if not name.startswith(".") and os.path.isdir(path):
                found.append(path)
    return found


def state():
    conn = db()
    rows = conn.execute(
        "SELECT id, video_path, srt_path, offset_ms, confidence, status, attempts,"
        " backup_path, created_at FROM sync_jobs ORDER BY id DESC LIMIT 2000"
    ).fetchall()

    counts = {}
    for status, total in conn.execute("SELECT status, COUNT(*) FROM sync_jobs GROUP BY status"):
        counts[status] = total

    answer = {
        "paused": setting(conn, "paused", "0") == "1",
        "interval": int(setting(conn, "scan_interval", config["interval"])),
        "excluded": json.loads(setting(conn, "excluded", "[]")),
        "folders": folders(),
        "roots": config["roots"],
        "counts": counts,
        "queued": config["jobs"].qsize(),
        "jobs": [dict(row) for row in rows],
    }
    conn.close()
    return answer


def undo(conn, ids):
    done = 0
    for job_id in ids:
        row = conn.execute("SELECT srt_path FROM sync_jobs WHERE id = ?", (job_id,)).fetchone()
        if not row:
            continue
        result = subprocess.run([config["lapse"], "--undo", row["srt_path"]],
                                capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            continue
        mtime = os.path.getmtime(row["srt_path"]) if os.path.exists(row["srt_path"]) else None
        conn.execute("UPDATE sync_jobs SET status = 'undone', backup_path = NULL, srt_mtime = ? WHERE id = ?",
                     (mtime, job_id))
        done += 1
    conn.commit()
    return done


def resync(conn, ids):
    if ids:
        marks = ",".join("?" * len(ids))
        rows = conn.execute("SELECT srt_path FROM sync_jobs WHERE id IN (%s)" % marks, ids).fetchall()
        conn.execute("DELETE FROM sync_jobs WHERE id IN (%s)" % marks, ids)
        directories = {os.path.dirname(row["srt_path"]) for row in rows}
    else:
        conn.execute("DELETE FROM sync_jobs")
        directories = set(config["roots"])
    conn.commit()

    for directory in directories:
        config["jobs"].put(directory)
    return len(directories)


def wanted_ids(body):
    return [int(value) for value in body.get("ids", [])]


def act(path, body):
    conn = db()
    try:
        if path == "/api/pause":
            save_setting(conn, "paused", "1" if body.get("paused") else "0")
        elif path == "/api/interval":
            save_setting(conn, "scan_interval", max(0, int(body.get("seconds", 0))))
        elif path == "/api/excluded":
            paths = [p.strip() for p in body.get("paths", []) if isinstance(p, str) and p.strip()]
            save_setting(conn, "excluded", json.dumps(sorted(set(paths))))
        elif path == "/api/undo":
            undo(conn, wanted_ids(body))
        elif path == "/api/resync":
            resync(conn, wanted_ids(body))
        elif path == "/api/scan":
            for root in config["roots"]:
                config["jobs"].put(root)
        else:
            return None
    finally:
        conn.close()
    return state()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, payload, code=200):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.split("?")[0] == "/api/state":
            self.reply(state())
            return
        if self.path.split("?")[0] not in ("/", "/index.html"):
            self.reply({"error": "not found"}, 404)
            return
        with open(PAGE, "rb") as page:
            body = page.read()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            self.reply({"error": "that was not json"}, 400)
            return

        try:
            answer = act(self.path.split("?")[0], body)
        except Exception as e:
            self.reply({"error": str(e)}, 400)
            return

        if answer is None:
            self.reply({"error": "not found"}, 404)
        else:
            self.reply(answer)


def start(port, jobs, roots, db_path, lapse, interval):
    config.update({"jobs": jobs, "roots": roots, "db_path": db_path,
                   "lapse": lapse, "interval": interval})

    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server
