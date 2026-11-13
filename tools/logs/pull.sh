#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Pull the three session-log generations from the Vita over VitaShell's FTP
# server into logs/. Read-only: it never writes to the console.
#
#   tools/logs/pull.sh <vita-ip>
#
# VitaShell must be running its FTP server on port 1337. The app's own server
# is not used, because a failed server is one of the things this log diagnoses.

set -eu

IP="${1:-}"
if [ -z "$IP" ]; then
    echo "usage: tools/logs/pull.sh <vita-ip>" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/logs"
mkdir -p "$DEST"

for name in session.log session.old.log session.old2.log; do
    url="ftp://$IP:1337/ux0:/data/vitaFTPShell/$name"
    tmp="$DEST/$name.pull"
    if curl -fsS --connect-timeout 5 --max-time 30 "$url" -o "$tmp"; then
        mv "$tmp" "$DEST/$name"
        printf 'pulled %s (%s bytes)\n' "$name" "$(wc -c < "$DEST/$name" | tr -d ' ')"
    else
        rm -f "$tmp"
        printf 'not present: %s\n' "$name"
    fi
done
