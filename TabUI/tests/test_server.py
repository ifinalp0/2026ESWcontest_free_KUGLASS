from __future__ import annotations

import io
import os
import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from server import TabUIHandler, TabUIServer, parse_args, restart_current_process


class ServerDefaultsTests(unittest.TestCase):
    def test_live_defaults_to_macos_usb_auto_detection(self) -> None:
        with patch.dict(os.environ, {}, clear=True), patch.object(sys, "argv", ["server.py"]):
            args = parse_args()

        self.assertEqual(args.transport, "usb")
        self.assertEqual(args.usb_port, "auto")
        self.assertEqual(args.port, 8080)

    def test_restart_reexecutes_server_with_original_arguments(self) -> None:
        with (
            patch.object(sys, "argv", ["server.py", "--port", "9090", "--transport", "mock"]),
            patch("server.os.execv") as execv,
        ):
            restart_current_process()

        expected_script = str(Path(__file__).resolve().parents[1] / "server.py")
        execv.assert_called_once_with(
            sys.executable,
            [sys.executable, expected_script, "--port", "9090", "--transport", "mock"],
        )


class FakeGateway:
    def __init__(self) -> None:
        self.reconnect_count = 0
        self.runtime_running = True

    def start(self) -> bool:
        changed = not self.runtime_running
        self.runtime_running = True
        return changed

    def stop(self) -> bool:
        changed = self.runtime_running
        self.runtime_running = False
        return changed

    def reconnect_controller(self) -> dict[str, object]:
        self.reconnect_count += 1
        return {"requested": True, "portOpen": True, "link": {"hardwareConnected": False}}

    def snapshot(self) -> dict[str, object]:
        return {
            "schemaVersion": 1,
            "link": {
                "backendRunning": self.runtime_running,
                "hardwareConnected": False,
            },
        }


class FakeServer:
    def __init__(self) -> None:
        self.gateway = FakeGateway()
        self.restart_requested = threading.Event()
        self.restart_count = 0

    def request_restart(self) -> bool:
        self.restart_count += 1
        self.restart_requested.set()
        return True


class MaintenanceEndpointTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = FakeServer()

    def post(self, path: str) -> tuple[int, dict[str, object]]:
        handler = object.__new__(TabUIHandler)
        handler.path = path
        handler.server = self.server  # type: ignore[assignment]
        handler.wfile = io.BytesIO()
        response: list[tuple[int, dict[str, object]]] = []
        handler._send_json = (  # type: ignore[method-assign]
            lambda payload, code=200, head_only=False: response.append((code, payload))
        )
        handler.do_POST()
        return response[0]

    def test_controller_reconnect_endpoint_returns_fresh_state(self) -> None:
        status, payload = self.post("/api/controller/reconnect")

        self.assertEqual(status, 202)
        self.assertTrue(payload["ok"])
        self.assertTrue(payload["requested"])
        self.assertEqual(self.server.gateway.reconnect_count, 1)
        self.assertIn("state", payload)

    def test_backend_power_endpoints_keep_http_shell_available(self) -> None:
        status, payload = self.post("/api/backend/stop")
        self.assertEqual(status, 202)
        self.assertTrue(payload["changed"])
        self.assertFalse(payload["running"])
        self.assertFalse(payload["state"]["link"]["backendRunning"])

        status, payload = self.post("/api/backend/start")
        self.assertEqual(status, 202)
        self.assertTrue(payload["changed"])
        self.assertTrue(payload["running"])
        self.assertTrue(payload["state"]["link"]["backendRunning"])

    def test_server_restart_endpoint_requests_shutdown_after_response(self) -> None:
        status, payload = self.post("/api/server/restart")

        self.assertEqual(status, 202)
        self.assertTrue(payload["restarting"])
        self.assertTrue(self.server.restart_requested.is_set())
        self.assertEqual(self.server.restart_count, 1)

    def test_server_request_restart_schedules_shutdown_only_once(self) -> None:
        tabui_server = object.__new__(TabUIServer)
        tabui_server.restart_requested = threading.Event()
        tabui_server.shutdown = Mock()

        with patch("server.threading.Thread") as thread:
            self.assertTrue(tabui_server.request_restart())
            self.assertFalse(tabui_server.request_restart())

        thread.assert_called_once_with(
            target=tabui_server.shutdown,
            name="tabui-server-restart",
            daemon=True,
        )
        thread.return_value.start.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
