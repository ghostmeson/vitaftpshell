#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# build.sh leaves exactly one versioned VPK. There is no fixed fallback: a
# missing artifact must be an error, not a request to upload the wrong build.
VPK="$(ls -t "$ROOT"/build/vitaftpshell-v*.vpk 2>/dev/null | head -1 || true)"
if [[ -z "$VPK" ]]; then
  echo "No vitaftpshell-v*.vpk in build/. Run ./build.sh first." >&2
  exit 1
fi

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <vita-ip>" >&2
  echo "Example: $0 192.168.1.42" >&2
  exit 1
fi

VITA_IP="$1"
NAME="$(basename "$VPK")"
BUILD="$(cat "$ROOT/BUILD_NUMBER" 2>/dev/null || true)"

# A failed build leaves the previous VPK in build/. Refuse to upload an artifact
# whose number does not match the current BUILD_NUMBER: that is how a stale
# build reaches the console while the user thinks it is the new one.
case "$NAME" in
  *-b"$BUILD".vpk) ;;
  *)
    echo "Newest VPK ($NAME) does not match BUILD_NUMBER ($BUILD)." >&2
    echo "The last build failed or is incomplete. Run ./build.sh again." >&2
    exit 1
    ;;
esac

echo "Uploading: $NAME"
echo "  to ftp://$VITA_IP:1337/ux0:/data/ ..."
curl -T "$VPK" "ftp://$VITA_IP:1337/ux0:/data/"

echo
echo "Uploaded. On the Vita:"
echo "  1. Open VitaShell"
echo "  2. Go to ux0:data"
echo "  3. Select $NAME and press Cross"
echo "  4. Launch the vitaFTPShell bubble"
