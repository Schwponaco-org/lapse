#!/bin/bash
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
# Usage: package.sh <name> <stage-dir> <out-dir>
set -e

NAME=${1:?usage: package.sh <name> <stage-dir> <out-dir>}
STAGE=${2:?usage: package.sh <name> <stage-dir> <out-dir>}
OUT=${3:?usage: package.sh <name> <stage-dir> <out-dir>}

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
STAGE=$(cd "$STAGE" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cp -R "$STAGE" "$WORK/$NAME"
cp "$ROOT/LICENSE" "$ROOT/README.md" "$WORK/$NAME/"
chmod +x "$WORK/$NAME"/lapse* 2>/dev/null || true

cd "$WORK"
case "$NAME" in
    *windows*) zip -qr "$OUT/$NAME.zip" "$NAME" ;;
    *)         tar czf "$OUT/$NAME.tar.gz" "$NAME" ;;
esac

ls -la "$OUT"
