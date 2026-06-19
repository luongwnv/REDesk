#!/usr/bin/env bash
# Build the REDesk Linux (.deb + binary) via Docker and drop artifacts in ./dist.
# Usage: ./packaging/linux/build.sh   (run from the repo root)
set -euo pipefail

cd "$(dirname "$0")/../.."   # repo root

echo "==> Building Ubuntu image (Qt6 + REDesk)…"
docker build -f packaging/linux/Dockerfile -t redesk-linux .

echo "==> Extracting artifacts to ./dist …"
mkdir -p dist
docker run --rm -v "$PWD/dist:/out" redesk-linux

echo "==> Done. Artifacts:"
ls -lh dist/
