"""Small host-power state fixture. NOT real BMC hardware or Redfish firmware."""
from __future__ import annotations

import argparse
import hmac
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class HostState:
    def __init__(self):
        self.lock = threading.RLock()
        self.power = "On"
        self.boots = 1

    def snapshot(self):
        with self.lock:
            return {"PowerState": self.power, "BootCount": self.boots}

    def reset(self, reset_type):
        with self.lock:
            if reset_type == "On":
                if self.power == "Off":
                    self.boots += 1
                self.power = "On"
            elif reset_type == "ForceOff":
                self.power = "Off"
            elif reset_type == "ForceRestart":
                self.power = "On"
                self.boots += 1
            else:
                raise ValueError("Unsupported ResetType")
            return self.snapshot()


class AgentHandler(BaseHTTPRequestHandler):
    server: "HostServer"

    def log_message(self, *_):
        pass

    def send_json(self, status, obj):
        data = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def authenticate(self):
        supplied = self.headers.get("X-Agent-Key", "")
        if not hmac.compare_digest(supplied, self.server.agent_key):
            self.send_json(403, {"error": "Internal management access denied"})
            return False
        return True

    def do_GET(self):
        if not self.authenticate():
            return
        if self.path == "/internal/state":
            self.send_json(200, self.server.state.snapshot())
        else:
            self.send_json(404, {"error": "Not Found"})

    def do_POST(self):
        if not self.authenticate():
            return
        if self.path == "/internal/fixture-reset":
            self.server.state = HostState()
            return self.send_json(200, {"status": "reset"})
        if self.path != "/internal/reset":
            return self.send_json(404, {"error": "Not Found"})
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            return self.send_json(415, {"error": "JSON required"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length > 1024 or length < 1:
                raise ValueError("Invalid body size")
            obj = json.loads(self.rfile.read(length))
            if not isinstance(obj, dict) or set(obj) != {"ResetType"}:
                raise ValueError("Expected ResetType")
            state = self.server.state.reset(obj["ResetType"])
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError, TypeError):
            return self.send_json(400, {"error": "Invalid reset request"})
        self.send_json(200, state)


class HostServer(ThreadingHTTPServer):
    def __init__(self, address, secret):
        super().__init__(address, AgentHandler)
        self.agent_key = secret
        self.state = HostState()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    secret = os.environ.get("HOST_AGENT_KEY", "")
    if len(secret) < 12:
        parser.error("HOST_AGENT_KEY must have at least 12 characters")
    srv = HostServer((args.bind, args.port), secret)
    print(f"READY host={srv.server_address[0]}:{srv.server_address[1]}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
