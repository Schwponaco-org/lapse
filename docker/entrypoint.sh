#!/bin/sh
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

set -e

if [ -n "$PUID" ] || [ -n "$PGID" ]; then
    USER_ID=${PUID:-0}
    GROUP_ID=${PGID:-0}
    DB_DIR=$(dirname "${DB_PATH:-/data/lapse.db}")
    mkdir -p "$DB_DIR"
    chown -R "$USER_ID:$GROUP_ID" "$DB_DIR" 2>/dev/null || true
    echo "Running as $USER_ID:$GROUP_ID"
    exec gosu "$USER_ID:$GROUP_ID" "$@"
fi

exec "$@"
