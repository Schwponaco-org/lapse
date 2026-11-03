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

# Runs lapse-tui in a pseudo terminal and reads the screen back the way a
# terminal would draw it, so the checks below look at what a person sees.
#
#   drive-tui.py <lapse-tui> <library>            the checks smoke-tui.sh runs
#   drive-tui.py <lapse-tui> <library> --show     start it, print the screen, quit

import codecs
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import termios
import time
import unicodedata


class Screen:
    """Just enough of a terminal for what FTXUI sends: cursor moves, erasing,
    colours, the alternate screen and line wrap turned off."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.grid = self.blank(rows, cols)
        self.saved = None
        self.y = self.x = 0
        self.fg = None
        self.bold = self.dim = False
        self.wrap = True
        self.cursor = True
        self.alt = False
        self.state = "text"
        self.buf = ""
        self.replies = []
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    @staticmethod
    def cell(ch=" "):
        return {"ch": ch, "fg": None, "bold": False, "dim": False}

    def blank(self, rows, cols):
        return [[self.cell() for _ in range(cols)] for _ in range(rows)]

    def resize(self, rows, cols):
        grid = self.blank(rows, cols)
        for y in range(min(rows, self.rows)):
            for x in range(min(cols, self.cols)):
                grid[y][x] = self.grid[y][x]
        self.grid, self.rows, self.cols = grid, rows, cols
        self.y, self.x = min(self.y, rows - 1), min(self.x, cols - 1)

    def feed(self, data):
        for ch in self.decoder.decode(data):
            self.put(ch)

    def put(self, ch):
        state = self.state
        if state == "esc":
            self.state = {"[": "csi", "]": "osc", "P": "dcs", "(": "skip", ")": "skip"}.get(ch, "text")
            self.buf = ""
        elif state == "skip":
            self.state = "text"
        elif state == "csi":
            if "\x40" <= ch <= "\x7e":
                self.state = "text"
                self.csi(self.buf, ch)
            else:
                self.buf += ch
        elif state in ("osc", "dcs"):
            if ch == "\x07":
                self.state = "text"
            elif ch == "\x1b":
                self.state = "skip"
        elif ch == "\x1b":
            self.state = "esc"
        elif ch == "\r":
            self.x = 0
        elif ch == "\n":
            self.down()
        elif ch == "\b":
            self.x = max(0, self.x - 1)
        elif ord(ch) >= 32:
            self.write(ch)

    def down(self):
        if self.y == self.rows - 1:
            self.grid.pop(0)
            self.grid.append([self.cell() for _ in range(self.cols)])
        else:
            self.y += 1

    def write(self, ch):
        if unicodedata.combining(ch):
            return
        width = 2 if unicodedata.east_asian_width(ch) in "WF" else 1
        if self.x + width > self.cols:
            if not self.wrap:
                self.x = self.cols - width
            else:
                self.x = 0
                self.down()
        here = self.grid[self.y][self.x]
        here.update(ch=ch, fg=self.fg, bold=self.bold, dim=self.dim)
        if width == 2 and self.x + 1 < self.cols:
            self.grid[self.y][self.x + 1] = self.cell("")
        self.x = min(self.x + width, self.cols if self.wrap else self.cols - 1)

    def numbers(self, params, default):
        out = []
        for part in params.split(";"):
            out.append(int(part) if part.isdigit() else default)
        return out

    def erase(self, y, start, end):
        for x in range(max(0, start), min(self.cols, end)):
            self.grid[y][x] = self.cell()

    def csi(self, params, final):
        private = params.startswith("?")
        if private:
            params = params[1:]
        if final == "m":
            self.sgr(params)
            return
        if private and final in "hl":
            on = final == "h"
            for mode in self.numbers(params, 0):
                if mode == 1049 and on != self.alt:
                    self.alt = on
                    if on:
                        self.saved = self.grid
                        self.grid = self.blank(self.rows, self.cols)
                    elif self.saved is not None:
                        self.grid = self.saved
                        self.resize(self.rows, self.cols)
                elif mode == 7:
                    self.wrap = on
                elif mode == 25:
                    self.cursor = on
            return
        if not params.replace(";", "").isdigit() and params != "":
            return
        n = self.numbers(params, 1)
        first = n[0] if n else 1
        if final in "Hf":
            row = n[0] if len(n) > 0 else 1
            col = n[1] if len(n) > 1 else 1
            self.y, self.x = min(self.rows, max(1, row)) - 1, min(self.cols, max(1, col)) - 1
        elif final == "A":
            self.y = max(0, self.y - first)
        elif final == "B":
            self.y = min(self.rows - 1, self.y + first)
        elif final == "C":
            self.x = min(self.cols - 1, self.x + first)
        elif final == "D":
            self.x = max(0, self.x - first)
        elif final == "G":
            self.x = min(self.cols, max(1, first)) - 1
        elif final == "d":
            self.y = min(self.rows, max(1, first)) - 1
        elif final == "J":
            how = self.numbers(params, 0)[0] if params else 0
            if how in (0, 2, 3):
                self.erase(self.y, self.x if how == 0 else 0, self.cols)
                for y in range(self.y + 1, self.rows):
                    self.erase(y, 0, self.cols)
            if how in (1, 2, 3):
                for y in range(0, self.y):
                    self.erase(y, 0, self.cols)
                self.erase(self.y, 0, self.x + 1 if how == 1 else self.cols)
        elif final == "K":
            how = self.numbers(params, 0)[0] if params else 0
            if how == 0:
                self.erase(self.y, self.x, self.cols)
            elif how == 1:
                self.erase(self.y, 0, self.x + 1)
            else:
                self.erase(self.y, 0, self.cols)
        elif final == "X":
            self.erase(self.y, self.x, self.x + first)
        elif final == "n" and params == "6":
            self.replies.append("\x1b[%d;%dR" % (self.y + 1, self.x + 1))

    def sgr(self, params):
        codes = [int(p) if p.isdigit() else 0 for p in re.split("[;:]", params)] if params else [0]
        i = 0
        while i < len(codes):
            c = codes[i]
            if c == 0:
                self.fg, self.bold, self.dim = None, False, False
            elif c == 1:
                self.bold = True
            elif c == 2:
                self.dim = True
            elif c == 22:
                self.bold = self.dim = False
            elif 30 <= c <= 37:
                self.fg = c - 30
            elif 90 <= c <= 97:
                self.fg = c - 90 + 8
            elif c == 39:
                self.fg = None
            elif c in (38, 48):
                if i + 1 < len(codes) and codes[i + 1] == 5:
                    if c == 38 and i + 2 < len(codes):
                        self.fg = codes[i + 2]
                    i += 2
                elif i + 1 < len(codes) and codes[i + 1] == 2:
                    if c == 38:
                        self.fg = tuple(codes[i + 2:i + 5])
                    i += 4
            i += 1

    def line(self, y):
        return "".join(c["ch"] for c in self.grid[y])

    def text(self):
        return "\n".join(self.line(y) for y in range(self.rows))

    def find(self, word):
        for y in range(self.rows):
            x = self.line(y).find(word)
            if x >= 0:
                return y, x
        return None


class Session:
    def __init__(self, argv, rows, cols, env=None):
        self.master, self.slave = os.openpty()
        self.set_size(rows, cols)
        self.before = termios.tcgetattr(self.slave)
        self.screen = Screen(rows, cols)
        self.proc = subprocess.Popen(
            argv, stdin=self.slave, stdout=self.slave, stderr=self.slave, env=env,
            start_new_session=True,
            preexec_fn=lambda: fcntl.ioctl(0, termios.TIOCSCTTY, 0))

    def set_size(self, rows, cols):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def pump(self, wait):
        ready, _, _ = select.select([self.master], [], [], wait)
        if not ready:
            return False
        try:
            data = os.read(self.master, 65536)
        except OSError:
            return False
        self.screen.feed(data)
        while self.screen.replies:
            os.write(self.master, self.screen.replies.pop(0).encode())
        return bool(data)

    def settle(self, quiet=0.4, limit=10):
        end = time.time() + limit
        while time.time() < end:
            if not self.pump(quiet):
                return

    def wait(self, what, check, limit=90):
        end = time.time() + limit
        while time.time() < end:
            if check(self.screen):
                self.settle(0.2, 2)
                return
            self.pump(0.2)
        fail("waited %ds for %s" % (limit, what), self.screen)

    def send(self, keys, pause=0.25):
        # Esc with something straight after it reads as Alt and that key, so
        # it goes on its own, the way a person would press it
        for part in re.split("(\x1b(?!\\[))", keys):
            if part:
                os.write(self.master, part.encode())
                self.settle(pause if part != ESC else 0.3)

    def resize(self, rows, cols):
        self.screen.resize(rows, cols)
        self.set_size(rows, cols)
        self.settle(0.5)


def fail(why, screen=None):
    print("FAIL:", why)
    if screen is not None:
        print("---- screen %dx%d ----" % (screen.cols, screen.rows))
        print(screen.text())
        print("----")
    sys.exit(1)


UP, DOWN, ENTER, ESC = "\x1b[A", "\x1b[B", "\r", "\x1b"
GREEN, YELLOW, RED = (2, 10), (3, 11), (1, 9)
VERDICTS = {"solid": GREEN, "unsure": YELLOW, "nothing": RED}


def starts(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    found = re.findall(r"(\d+):(\d\d):(\d\d)[,.](\d\d\d)\s*-->", text)
    return [((int(h) * 60 + int(m)) * 60 + int(s)) * 1000 + int(ms) for h, m, s, ms in found]


def moved(path, original):
    now, then = starts(path), starts(original)
    if not now or len(now) != len(then):
        return None
    return now[0] - then[0]


def row_of(screen, name):
    """The list row for a file, as (y, line)."""
    at = screen.find(name)
    return (at[0], screen.line(at[0])) if at else (None, "")


def verdict_on(screen, name):
    y, line = row_of(screen, name)
    if y is None:
        return None, None
    for word in ("(solid)", "(unsure)", "(nothing)", "(in sync)", "(shifted)",
                 "solid", "unsure", "nothing", "in sync", "too few", "error", "undone", "shifted"):
        x = line.find(" " + word)
        if x >= 0:
            return word, screen.grid[y][x + 1]
    return None, None


def check_layout(screen, why):
    rows, cols = screen.rows, screen.cols
    left = max(36, min(cols * 2 // 3, cols - 28))
    want = {(1, 0): "╭", (1, left - 1): "╮", (1, left): "╭", (1, cols - 1): "╮",
            (rows - 3, 0): "╰", (rows - 3, left - 1): "╯", (rows - 3, left): "╰", (rows - 3, cols - 1): "╯"}
    for y in range(2, rows - 3):
        for x in (0, left - 1, left, cols - 1):
            want[(y, x)] = "│"
    for (y, x), ch in want.items():
        if screen.grid[y][x]["ch"] != ch:
            fail("%s: expected %r at row %d column %d, found %r" % (why, ch, y, x, screen.grid[y][x]["ch"]), screen)
    if not screen.line(0).startswith(" LAPSE "):
        fail(why + ": no header on the first line", screen)
    if "Quit" not in screen.line(rows - 1):
        fail(why + ": no key bar on the last line", screen)


def main():
    if len(sys.argv) < 3:
        print(__doc__ if __doc__ else "usage: drive-tui.py <lapse-tui> <library> [--show]")
        sys.exit(2)
    tui, library = sys.argv[1], os.path.abspath(sys.argv[2])
    env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8")
    session = Session([tui, library], 24, 80, env)
    screen = session.screen

    session.wait("the list to come up", lambda s: s.find("Show.S01E01.en.srt"))
    for bad in ("terminate called", "what():", "Aborted", "Segmentation"):
        if screen.find(bad):
            fail("the screen says " + bad, screen)
    check_layout(screen, "at 80x24")

    if "--show" in sys.argv:
        print(screen.text())
        session.send("q")
        session.proc.wait(10)
        return

    folder = os.path.join(library, "TV", "Show", "Season 01")
    first = os.path.join(folder, "Show.S01E01.en.srt")
    second = os.path.join(folder, "Show.S01E02.da.srt")
    keep = {}
    for path in (first, second):
        keep[path] = open(path, "rb").read()
        with open(path + ".orig", "wb") as f:
            f.write(keep[path])

    print("== sync one file through the list")
    session.send("/S01E01" + ENTER)
    if screen.find("Show.S01E02.da.srt"):
        fail("the filter left the other episode in the list", screen)
    session.send(" ")
    if not screen.find("1 selected"):
        fail("Space did not select it", screen)
    # force, so the file is written even when the verdict is only unsure,
    # which it can be with libfvad listening instead of Silero
    session.send("f" + ENTER, 0.1)
    session.send("q", 0.3)
    if not screen.find("still being synced"):
        fail("q did not ask first while a sync was running", screen)
    session.send("n")
    if screen.find("still being synced"):
        fail("n did not take the question away", screen)
    session.wait("a verdict", lambda s: verdict_on(s, "Show.S01E01.en.srt")[0] in VERDICTS)
    word, cell = verdict_on(screen, "Show.S01E01.en.srt")
    if cell["fg"] not in VERDICTS[word]:
        fail("%s is drawn in colour %r" % (word, cell["fg"]), screen)
    at = screen.find("Offset")
    shown = re.search(r"Offset\s+([+-]?\d+) ms", screen.line(at[0])) if at else None
    if not shown or abs(int(shown.group(1)) + 4200) > 300:
        fail("the details do not show an offset near -4200 ms", screen)
    by = moved(first, first + ".orig")
    if by is None or abs(by + 4200) > 300:
        fail("the subtitle moved by %r ms, it was 4200 ms late" % by, screen)
    if not os.path.exists(first + ".bak"):
        fail("no .bak was kept", screen)
    if "•" not in row_of(screen, "Show.S01E01.en.srt")[1]:
        fail("the row does not show the .bak", screen)
    print("   %s, offset %s ms, the file moved by %d ms" % (word, shown.group(1), by))
    session.send("f")

    print("== a dry run writes nothing")
    session.send(ESC + "/S01E02" + ENTER + "d" + ENTER)
    session.wait("a dry run verdict", lambda s: (verdict_on(s, "Show.S01E02.da.srt")[0] or "").startswith("("))
    word, cell = verdict_on(screen, "Show.S01E02.da.srt")
    # dim, or light grey on the highlighted row, where dim would not show
    if not cell["dim"] and cell["fg"] not in (7, 8):
        fail("the dry run verdict %s is not dim" % word, screen)
    if open(second, "rb").read() != keep[second] or os.path.exists(second + ".bak"):
        fail("the dry run changed something", screen)
    if not screen.find("dry run"):
        fail("the details do not say it was a dry run", screen)
    print("   " + word)
    session.send("d")

    print("== u puts the .bak back")
    session.send(ESC + "/S01E01" + ENTER + "u")
    if not screen.find("Put Show.S01E01.en.srt.bak back"):
        fail("u did not ask first", screen)
    session.send("y")
    session.wait("the undo", lambda s: verdict_on(s, "Show.S01E01.en.srt")[0] == "undone")
    if open(first, "rb").read() != keep[first] or os.path.exists(first + ".bak"):
        fail("the original did not come back, or the .bak is still there", screen)

    print("== s moves it by hand")
    session.send("s")
    if not screen.find("Move every line of"):
        fail("s did not ask how far", screen)
    session.send("-500" + ENTER)
    session.wait("the shift", lambda s: verdict_on(s, "Show.S01E01.en.srt")[0] == "shifted")
    by = moved(first, first + ".orig")
    if by != -500:
        fail("the shift moved the first line by %r ms, not -500" % by, screen)
    session.send("u")
    session.send("y")
    session.wait("the second undo", lambda s: verdict_on(s, "Show.S01E01.en.srt")[0] == "undone")

    print("== resizing")
    session.send(ESC)
    session.resize(40, 132)
    check_layout(screen, "at 132x40")
    if not screen.find("Show.S01E02.da.srt") or not screen.find("Show.S01E01.1080p"):
        fail("the wider list does not show the videos", screen)
    session.resize(24, 80)
    check_layout(screen, "back at 80x24")

    session.send("?")
    if not screen.find("Any key closes this"):
        fail("? did not show the keys", screen)
    session.send(" ")
    if screen.find("Any key closes this"):
        fail("the help did not close", screen)
    check_layout(screen, "after the help")

    print("== quitting")
    session.send("q", 0.5)
    try:
        code = session.proc.wait(10)
    except subprocess.TimeoutExpired:
        fail("q did not quit", screen)
    session.settle(0.3, 2)
    if code != 0:
        fail("it exited with %d" % code, screen)
    if screen.alt or not screen.cursor or not screen.wrap:
        fail("the terminal was left with the alternate screen, no cursor or no line wrap", screen)
    after = termios.tcgetattr(session.slave)
    if after[3] != session.before[3] or after[0] != session.before[0]:
        fail("the terminal settings were not put back")
    if not screen.find("lapse-tui:"):
        fail("no summary after quitting", screen)
    print("   " + screen.line(screen.find("lapse-tui:")[0]).strip())


if __name__ == "__main__":
    main()
