#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
DIST_DIR="$ROOT/components/wifi-manager/webapp/dist"

if [[ ! -d "$DIST_DIR" ]]; then
    echo "dist directory not found: $DIST_DIR" >&2
    echo "Build webapp first (e.g. npm run build in components/wifi-manager/webapp)." >&2
    exit 1
fi

python - "$DIST_DIR" <<'PY'
import os
import sys
import glob
from datetime import datetime, timezone

dist = sys.argv[1]

def rel(p):
    return os.path.relpath(p, start=os.getcwd())

all_files = [p for p in glob.glob(os.path.join(dist, "**", "*"), recursive=True) if os.path.isfile(p)]
ship_files = [p for p in all_files if p.endswith(".gz") or p.endswith(".png")]

all_files_sorted = sorted([(os.path.getsize(p), p) for p in all_files], reverse=True)
ship_sorted = sorted([(os.path.getsize(p), p) for p in ship_files], reverse=True)

print(f"Snapshot UTC: {datetime.now(timezone.utc).isoformat(timespec='seconds')}")
print(f"Dist dir: {rel(dist)}")
print("")
print(f"All dist files: {len(all_files)}")
print(f"All dist bytes: {sum(s for s, _ in all_files_sorted)}")
print(f"Shipped files (*.gz + *.png): {len(ship_sorted)}")
print(f"Shipped bytes: {sum(s for s, _ in ship_sorted)}")
print("")
print("Top 15 dist files:")
for size, path in all_files_sorted[:15]:
    print(f"{size:9d}  {rel(path)}")
print("")
print("Shipped files:")
for size, path in ship_sorted:
    print(f"{size:9d}  {rel(path)}")
PY
