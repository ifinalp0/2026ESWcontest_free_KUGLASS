#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ -f "$ROOT_DIR/.env" ]; then
  set -a
  # shellcheck disable=SC1091
  . "$ROOT_DIR/.env"
  set +a
fi

SIMUL_TWIN_HOST=${SIMUL_TWIN_HOST:-127.0.0.1}
SIMUL_TWIN_PORT=${SIMUL_TWIN_PORT:-5050}
VITE_BACKEND_URL=${VITE_BACKEND_URL:-http://localhost:$SIMUL_TWIN_PORT}
export SIMUL_TWIN_HOST SIMUL_TWIN_PORT VITE_BACKEND_URL

if [ ! -x "$ROOT_DIR/backend/.venv/bin/python" ]; then
  echo "Missing backend virtualenv. Run: cd Simul_Twin/backend && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt" >&2
  exit 1
fi

if [ ! -d "$ROOT_DIR/frontend/node_modules" ]; then
  echo "Missing frontend dependencies. Run: cd Simul_Twin/frontend && npm install" >&2
  exit 1
fi

cleanup() {
  if [ -n "${BACKEND_PID:-}" ]; then
    kill "$BACKEND_PID" 2>/dev/null || true
  fi
  if [ -n "${FRONTEND_PID:-}" ]; then
    kill "$FRONTEND_PID" 2>/dev/null || true
  fi
}
trap cleanup INT TERM EXIT

cd "$ROOT_DIR/backend"
.venv/bin/python app.py &
BACKEND_PID=$!

cd "$ROOT_DIR/frontend"
npm run dev &
FRONTEND_PID=$!

echo "Backend:  http://$SIMUL_TWIN_HOST:$SIMUL_TWIN_PORT"
echo "Frontend: http://localhost:5173"
wait "$BACKEND_PID" "$FRONTEND_PID"
