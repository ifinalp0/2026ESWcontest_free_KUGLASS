# KUGLASS TabUI

Tablet demonstration UI for the KUGLASS active smart glass mobility project.

This app is scoped to the tablet `/demo` surface described in
`Smart_glass_V20_0.md` and the submitted development plan:

- show LIVE/MOCK status, current vehicle situation, front glare, 4-way lux vector, weather, fault, and manual state
- demonstrate the priority scenarios: hot-summer thermal load, camping privacy, parked theft prevention, front camera saturation, and 360 degree flashlight
- display CH0-CH7 target/applied MI, optical state, selected channel, camera ROI evidence, and decision reasons
- send only UI commands to the Raspberry Pi UI service command queue; it never talks directly to ESP32 or power hardware
- fall back to an offline mock state so the tablet screen can still be rehearsed without hardware

## Run

From this directory:

```bash
python3 server.py --host 0.0.0.0 --port 5173 --api http://127.0.0.1:8080
```

Open:

```text
http://localhost:5173/demo
```

The `--api` target should point at `RasPi_Algo`'s `kuglass-ui.service`.
The TabUI server proxies `/api/state` and `/api/command` to that service so the tablet browser can use same-origin requests.

## Checks

```bash
npm run check
```

The check uses only local tools: Python bytecode compilation and JavaScript syntax validation.

