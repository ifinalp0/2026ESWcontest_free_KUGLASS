#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

cd "$ROOT_DIR/backend"
.venv/bin/python -m pytest

cd "$ROOT_DIR/frontend"
npm run typecheck
npm run build
