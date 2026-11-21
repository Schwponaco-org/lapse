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


import base64
import json
import os
import re
import select
import socket
import subprocess
import tempfile
import time

TIMEOUT = int(os.environ.get("TIMEOUT", "1800"))
MAX_BODY = 20 * 1024 * 1024

PROGRESS = re.compile(r"^(.+) (\d+)%$")


class Refused(Exception):
    def __init__(self, message, code=400):
        super().__init__(message)
        self.code = code


class Gone(Exception):
    pass


class Slow(Exception):
    pass


def version(lapse):
    result = subprocess.run([lapse, "--version"], capture_output=True, text=True, timeout=30)
    return {"version": result.stdout.strip()}


def decode(body, name):
    value = body.get(name)
    if not isinstance(value, str):
        raise Refused(name + " has to be base64 text")
    try:
        data = base64.b64decode(value, validate=True)
    except ValueError:
        raise Refused(name + " is not valid base64")
    return data


def extension(body, name, formats):
    filename = body.get(name)
    if not isinstance(filename, str):
        raise Refused(name + " has to be a file name")
    ext = os.path.splitext(os.path.basename(filename))[1].lower()
    if ext not in formats:
        raise Refused("lapse does not read " + (ext or "files without an extension") + " (" + name + ")")
    return ext


def track(body, name):
    value = body.get(name)
    if value is None:
        return None
    if not isinstance(value, int) or value < 0:
        raise Refused(name + " has to be a number from 0, or null")
    return value


def read_body(handler):
    try:
        length = int(handler.headers.get("Content-Length") or 0)
    except ValueError:
        length = 0
    if length <= 0:
        raise Refused("send a json body with a Content-Length")
    if length > MAX_BODY:
        raise Refused("that is more than %d MB" % (MAX_BODY // 1024 // 1024))
    try:
        body = json.loads(handler.rfile.read(length))
    except ValueError:
        raise Refused("that was not json")
    if not isinstance(body, dict):
        raise Refused("the body has to be a json object")
    return body


def client_gone(sock):
    try:
        return sock.recv(1, socket.MSG_PEEK) == b""
    except OSError:
        return True


# What lapse says on stderr, a line at a time, and None when a second went
# by without anything. Watches the client as well, since lapse can be quiet
# for a long while and nobody should wait for it to say something before
# we notice the other end hung up
def stderr_lines(child, sock, deadline):
    fd = child.stderr.fileno()
    watching = [fd, sock]
    rest = b""
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            raise Slow()
        ready = select.select(watching, [], [], min(left, 1))[0]
        if not ready:
            yield None    # a quiet second, the caller can show it is still alive
        if sock in ready:
            if client_gone(sock):
                raise Gone()
            watching.remove(sock)    # it sent something, so it is still there
        if fd in ready:
            data = os.read(fd, 4096)
            if not data:
                return
            rest += data
            *lines, rest = rest.split(b"\n")
            for line in lines:
                yield line.decode("utf-8", "replace").strip()


# chunked, so clients get every line right away and not when some buffer fills
def send(handler, data):
    handler.wfile.write(b"%x\r\n%s\r\n" % (len(data), data))
    handler.wfile.flush()


def say(handler, payload):
    send(handler, json.dumps(payload).encode() + b"\n")


def outcome(code, stdout, tail, out):
    try:
        done = json.loads(stdout)
    except ValueError:
        done = None
    if code not in (0, 2, 3) or not isinstance(done, dict):
        message = "lapse exited with %d" % code
        if tail:
            message += ": " + " / ".join(tail)
        return {"error": message}

    synced = None
    if done.get("written") and os.path.isfile(out):
        with open(out, "rb") as file:
            synced = base64.b64encode(file.read()).decode()
    return {"done": done, "subtitle": synced}


def run(handler, command, out):
    env = dict(os.environ, LAPSE_PROGRESS="1")
    child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        handler.send_response(200)
        handler.send_header("Content-Type", "application/x-ndjson")
        handler.send_header("Cache-Control", "no-cache")
        handler.send_header("Connection", "close")
        handler.send_header("Transfer-Encoding", "chunked")
        handler.end_headers()

        tail = []
        wrote = time.monotonic()
        try:
            for line in stderr_lines(child, handler.connection, time.monotonic() + TIMEOUT):
                found = PROGRESS.match(line or "")
                if found:
                    say(handler, {"progress": int(found.group(2)), "phase": found.group(1)})
                    wrote = time.monotonic()
                    continue
                if line:
                    tail = (tail + [line])[-5:]
                # an empty line is for the client to skip, and a write that fails tells us it left
                if time.monotonic() - wrote >= 1:
                    send(handler, b"\n")
                    wrote = time.monotonic()
            stdout = child.stdout.read()
            say(handler, outcome(child.wait(), stdout, tail, out))
        except Slow:
            say(handler, {"error": "lapse took longer than %d seconds" % TIMEOUT})
        handler.wfile.write(b"0\r\n\r\n")
    except (Gone, OSError):
        pass    # the client left, nobody is listening for an answer
    finally:
        if child.poll() is None:
            child.kill()
        child.wait()
        child.stdout.close()
        child.stderr.close()


def sync(handler, lapse, formats):
    # the answer is a stream, so the connection is not kept for another request
    handler.close_connection = True
    body = read_body(handler)

    subtitle = decode(body, "subtitle")
    sub_ext = extension(body, "filename", formats)
    reference = None
    if body.get("reference") is not None:
        reference = decode(body, "reference")
        ref_ext = extension(body, "reference_filename", formats)

    video = body.get("video")
    if video is not None and (not isinstance(video, str) or not os.path.isabs(video)):
        raise Refused("video has to be a full path, or null")
    if reference is None and video is None:
        raise Refused("send a video or a reference")

    audio_track = track(body, "audio_track")
    sub_track = track(body, "sub_track")
    no_embedded = bool(body.get("no_embedded"))

    if reference is None and not os.path.isfile(video):
        raise Refused(video + " is not in the lapse container. "
                      "Mount your media at the same path as in Bazarr.", 404)

    with tempfile.TemporaryDirectory(prefix="lapse-") as work:
        path = os.path.join(work, "subtitle" + sub_ext)
        out = os.path.join(work, "synced" + sub_ext)
        with open(path, "wb") as file:
            file.write(subtitle)

        against = video
        if reference is not None:
            against = os.path.join(work, "reference" + ref_ext)
            with open(against, "wb") as file:
                file.write(reference)

        command = [lapse, against, path, "--output", out, "--no-backup", "--no-sidecar", "--json"]
        if audio_track is not None:
            command += ["--audio-track", str(audio_track)]
        if sub_track is not None:
            command += ["--sub-track", str(sub_track)]
        if no_embedded:
            command.append("--no-embedded")

        run(handler, command, out)
