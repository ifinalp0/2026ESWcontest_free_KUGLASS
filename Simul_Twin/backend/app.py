from __future__ import annotations

import os
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request, send_from_directory
from flask_socketio import SocketIO

from simulator.engine import SimulationEngine


ROOT = Path(__file__).resolve().parent
FRONTEND_DIST = ROOT.parent / "frontend" / "dist"

app = Flask(
    __name__,
    static_folder=str(FRONTEND_DIST) if FRONTEND_DIST.exists() else None,
    static_url_path="",
)
app.config["SECRET_KEY"] = "kuglass-simul-twin"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")
engine = SimulationEngine()


@app.after_request
def add_cors_headers(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    response.headers["Access-Control-Allow-Methods"] = "GET,POST,OPTIONS"
    return response


@app.get("/health")
def health():
    return jsonify({"ok": True, "service": "simul-twin", "mode": "MOCK"})


@app.get("/api/state")
def state():
    return jsonify(engine.snapshot())


@app.get("/api/replay")
def replay():
    return jsonify({"events": engine.replay_events()})


@app.get("/api/replay/files")
def replay_files():
    return jsonify({"files": engine.replay_files()})


@app.post("/api/replay/save")
def replay_save():
    payload: dict[str, Any] = request.get_json(force=True, silent=True) or {}
    path = engine.save_replay(payload.get("name"))
    return jsonify({"ok": True, "path": str(path), "name": path.stem})


@app.post("/api/replay/load")
def replay_load():
    payload: dict[str, Any] = request.get_json(force=True, silent=True) or {}
    try:
        events = engine.load_replay(str(payload["name"]))
    except (FileNotFoundError, KeyError, ValueError) as error:
        return jsonify({"ok": False, "error": str(error)}), 400
    emit_all()
    return jsonify({"ok": True, "events": len(events)})


@app.post("/api/command")
def command():
    payload: dict[str, Any] = request.get_json(force=True, silent=True) or {}
    try:
        result = engine.apply_command(payload)
    except (FileNotFoundError, KeyError, ValueError) as error:
        return jsonify({"ok": False, "error": str(error)}), 400
    emit_all()
    return jsonify(result)


@app.route("/", defaults={"path": ""})
@app.route("/<path:path>")
def frontend(path: str):
    if FRONTEND_DIST.exists():
        target = FRONTEND_DIST / path
        if path and target.exists() and target.is_file():
            return send_from_directory(FRONTEND_DIST, path)
        return send_from_directory(FRONTEND_DIST, "index.html")
    return jsonify(
        {
            "service": "simul-twin-backend",
            "frontend": "not-built",
            "hint": "Run `npm run dev` in Simul_Twin/frontend.",
        }
    )


@socketio.on("connect")
def on_connect():
    emit_all()


@socketio.on("command")
def on_command(payload):
    engine.apply_command(payload or {})
    # Flashlight drag commands arrive continuously. The fixed-rate simulation
    # loop below publishes their resulting state without a four-event burst for
    # every pointer sample.
    if (payload or {}).get("type") != "setFlashlightAngle":
        emit_all()


def emit_all() -> None:
    snapshot = engine.snapshot()
    socketio.emit("state:fast", snapshot["fast"])
    socketio.emit("camera:metrics", snapshot["cameraMetrics"])
    socketio.emit("sensor:update", snapshot["environment"])
    socketio.emit("sim:decision", snapshot["decisionReason"])


def simulation_loop() -> None:
    tick = 0
    while True:
        engine.step(0.1)
        snapshot = engine.snapshot()
        socketio.emit("state:fast", snapshot["fast"])
        if tick % 2 == 0:
            socketio.emit("camera:metrics", snapshot["cameraMetrics"])
            socketio.emit("sim:decision", snapshot["decisionReason"])
        if tick % 10 == 0:
            socketio.emit("sensor:update", snapshot["environment"])
        tick += 1
        socketio.sleep(0.1)


if __name__ == "__main__":
    socketio.start_background_task(simulation_loop)
    socketio.run(
        app,
        host=os.environ.get("SIMUL_TWIN_HOST", "127.0.0.1"),
        port=int(os.environ.get("SIMUL_TWIN_PORT", "5050")),
        debug=os.environ.get("SIMUL_TWIN_DEBUG", "0") == "1",
        allow_unsafe_werkzeug=True,
    )
