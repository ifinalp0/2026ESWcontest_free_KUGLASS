from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any

from .models import to_jsonable
from .utils import now_ms


class StateStore:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, state: dict[str, Any]) -> None:
        payload = {"timestamp_ms": now_ms(), **to_jsonable(state)}
        fd, tmp_name = tempfile.mkstemp(prefix=self.path.name, dir=str(self.path.parent))
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
        os.replace(tmp_name, self.path)

    def read(self) -> dict[str, Any]:
        try:
            return json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {"timestamp_ms": now_ms(), "status": "empty"}
        except json.JSONDecodeError:
            return {"timestamp_ms": now_ms(), "status": "corrupt"}

