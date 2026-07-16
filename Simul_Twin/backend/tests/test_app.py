from __future__ import annotations

import app as app_module


def test_flashlight_stream_uses_fixed_rate_broadcast(monkeypatch) -> None:
    applied: list[dict] = []
    broadcasts: list[bool] = []
    monkeypatch.setattr(app_module.engine, "apply_command", lambda payload: applied.append(payload))
    monkeypatch.setattr(app_module, "emit_all", lambda: broadcasts.append(True))

    app_module.on_command({"type": "setFlashlightAngle", "angleDeg": 90})

    assert applied == [{"type": "setFlashlightAngle", "angleDeg": 90}]
    assert broadcasts == []

    app_module.on_command({"type": "setScenario", "demoMode": "none"})

    assert broadcasts == [True]
