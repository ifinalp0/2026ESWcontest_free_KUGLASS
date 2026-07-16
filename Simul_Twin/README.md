# KUGLASS Simul_Twin

Signal-based digital twin simulator for the KUGLASS active smart glass mobility project.

This simulator is intentionally not a precise optical or thermal physics engine. It is a planning and verification tool for:

- CH0-CH7 PDLC policy tuning before hardware is ready
- hot-summer, camping, parked, front-glare, and 360-degree flashlight demos
- manual Clear/Frost override behavior with auto-return TTL
- actuator fault injection, mock sensor dropout, and replay-safe UI flows
- a reusable prototype shape for the final `/demo` HMI

The 3D dashboard projects each PDLC channel film onto the loaded vehicle mesh at runtime. The film geometry therefore follows the actual windshield, door glass, rear glass, and roof surfaces instead of floating as independent flat planes.

## Structure

```text
Simul_Twin/
  backend/   Flask + Flask-SocketIO simulation service
  frontend/  React + Vite + React Three Fiber dashboard
```

## One-command development

```bash
cd Simul_Twin
sh scripts/dev.sh
```

Optional local settings can be copied from `.env.example` to `.env`.

## Policy tuning

Runtime policy constants live in `backend/config.yaml`. The simulator loads this file on backend startup and falls back to built-in defaults if a key is missing.

Useful sections:

- `thermal.channel_amount`: CH7 -> CH6 -> CH4/5 -> CH2/3 -> CH0/1 heat-load priority
- `directional`: 4-direction lux vector confidence and angular kernel
- `servo`: fast-attack, Frost, and Clear response rates
- `camera`: mock saturation and edge-retention response

## Backend

```bash
cd Simul_Twin/backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
```

The backend listens on `http://localhost:5050` and emits:

- `state:fast` at 10 Hz
- `camera:metrics` at 5 Hz
- `sensor:update` at 1 Hz
- `sim:decision` at 1-5 Hz

Replay APIs:

- `GET /api/replay`: in-memory replay buffer
- `GET /api/replay/files`: saved replay files
- `POST /api/replay/save` with `{ "name": "run_name" }`
- `POST /api/replay/load` with `{ "name": "run_name" }`

## Frontend

```bash
cd Simul_Twin/frontend
npm install
npm run dev
```

The frontend listens on Vite's default port, usually `http://localhost:5173`.

## Tests

```bash
cd Simul_Twin
sh scripts/check.sh
```

Frontend checks:

```bash
cd Simul_Twin/frontend
npm run typecheck
npm run build
```

## Safety Boundary

`Simul_Twin` never sends commands to ESP32 boards or PDLC hardware. It is a mock-only simulator aligned to the state and command shape planned for the final Raspberry Pi control/UI stack.
