from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .utils import now_ms


class CommandQueue:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.touch(exist_ok=True)
        self._offset = 0

    def push(self, payload: dict[str, Any]) -> None:
        record = {"timestamp_ms": now_ms(), "payload": payload}
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, separators=(",", ":")) + "\n")

    def pop_all(self) -> list[dict[str, Any]]:
        with self.path.open("r", encoding="utf-8") as handle:
            handle.seek(self._offset)
            lines = handle.readlines()
            self._offset = handle.tell()
        records: list[dict[str, Any]] = []
        for line in lines:
            try:
                records.append(json.loads(line)["payload"])
            except (KeyError, json.JSONDecodeError):
                continue
        return records

