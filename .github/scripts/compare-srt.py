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

# Prints how far the worst cue in the first file is from the second one

import re
import sys


def starts(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    found = re.findall(r"(\d+):(\d\d):(\d\d)[,.](\d\d\d)\s*-->", text)
    return [((int(h) * 60 + int(m)) * 60 + int(s)) * 1000 + int(ms) for h, m, s, ms in found]


got, want = starts(sys.argv[1]), starts(sys.argv[2])
if not got or len(got) != len(want):
    print(999999)
else:
    print(max(abs(a - b) for a, b in zip(got, want)))
