"""TLS-only, Redfish-inspired educational BMC REST service.

Deliberately a tiny subset, NOT DMTF Redfish compliant or NVIDIA hardware.
"""
from __future__ import annotations

import argparse
import hmac
import ipaddress
import json
import os
import ssl
import threading
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = "/redfish/v1"
BMC = ROOT + "/Managers/BMC"
HOST = ROOT + "/Systems/Host"
ETH = BMC + "/EthernetInterfaces/eth0"
EVENTS = HOST + "/LogServices/EventLog/Entries"
HOST_RESET = HOST + "/Actions/ComputerSystem.Reset"
BMC_RESET = BMC + "/Actions/Manager.Reset"


class Model:
    def __init__(self):
        self.lock = threading.RLock()
        self.generation = 1
        self.events = deque(maxlen=128)
        self.sequence = 0
        self.network = {"Address": "192.168.50.20", "SubnetMask": "255.255.255.0",
                        "Gateway": "192.168.50.1", "VLANId": 100}

    def event(self, message, severity="OK"):
        with self.lock:
            self.sequence += 1
            self.events.append({"Id": str(self.sequence), "Message": message, "Severity": severity})

    def logs(self):
        with self.lock:
            return list(self.events)

    def set_network(self, config):
        # Validate all properties before mutation (atomic PATCH semantics).
        if not isinstance(config, dict) or set(config) != {"IPv4", "VLANId"}:
            raise ValueError("Require IPv4 and VLANId")
        ipv4 = config["IPv4"]
        vlan = config["VLANId"]
        if not isinstance(ipv4, dict) or set(ipv4) != {"Address", "SubnetMask", "Gateway"}:
            raise ValueError("Invalid IPv4 fields")
        if type(vlan) is not int or not 1 <= vlan <= 4094:
            raise ValueError("VLAN must be 1..4094")
        try:
            address = ipaddress.IPv4Address(ipv4["Address"])
            gateway = ipaddress.IPv4Address(ipv4["Gateway"])
            net = ipaddress.IPv4Network((str(address), ipv4["SubnetMask"]), strict=False)
            if (net.prefixlen > 30 or address not in net or gateway not in net or
                    address in {net.network_address, net.broadcast_address} or
                    gateway in {net.network_address, net.broadcast_address} or address == gateway):
                raise ValueError("Unusable host/gateway address")
        except (ipaddress.AddressValueError, ipaddress.NetmaskValueError, TypeError, KeyError):
            raise ValueError("Invalid IPv4 subnet") from None
        with self.lock:
            self.network = {"Address": str(address), "SubnetMask": str(net.netmask),
                            "Gateway": str(gateway), "VLANId": vlan}
            self.event("Management NIC configuration updated")
            return dict(self.network)


class ManagementHandler(BaseHTTPRequestHandler):
    server: "ManagementServer"
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        # Never log bearer tokens or request headers in lab output.
        pass

    def respond(self, status, obj):
        body = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def role(self):
        header = self.headers.get("Authorization", "")
        prefix = "Bearer "
        if not header.startswith(prefix):
            return None
        token = header[len(prefix):]
        if hmac.compare_digest(token, self.server.admin_token):
            return "admin"
        if hmac.compare_digest(token, self.server.viewer_token):
            return "viewer"
        return None

    def authorize(self, write=False):
        role = self.role()
        if role is None:
            self.respond(401, {"error": "Valid bearer token required"})
            return False
        if write and role != "admin":
            self.respond(403, {"error": "Admin role required"})
            return False
        return True

    def agent(self, path, request_body=None):
        headers = {"X-Agent-Key": self.server.agent_key}
        data = None
        if request_body is not None:
            data = json.dumps(request_body).encode()
            headers["Content-Type"] = "application/json"
        req = Request(self.server.agent_url + path, data=data, headers=headers,
                      method="POST" if data is not None else "GET")
        try:
            with urlopen(req, timeout=1.5) as response:
                return json.load(response)
        except (URLError, HTTPError, OSError, json.JSONDecodeError):
            return None

    def read_json(self):
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            raise ValueError("application/json required")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise ValueError("Invalid content length") from None
        if length < 1 or length > 4096:
            raise ValueError("JSON payload must be 1..4096 bytes")
        try:
            obj = json.loads(self.rfile.read(length))
        except (ValueError, UnicodeDecodeError):
            raise ValueError("Malformed JSON") from None
        if not isinstance(obj, dict):
            raise ValueError("JSON object required")
        return obj

    def do_GET(self):
        if not self.authorize():
            return
        model = self.server.model
        if self.path == ROOT:
            return self.respond(200, {"Managers": {"@odata.id": BMC},
                                      "Systems": {"@odata.id": HOST}})
        if self.path == BMC:
            with model.lock:
                return self.respond(200, {"Id": "BMC", "Name": "Lab Management Controller",
                                          "Generation": model.generation, "FirmwareVersion": "lab-1.0",
                                          "EthernetInterfaces": {"@odata.id": ETH}})
        if self.path == HOST:
            result = self.agent("/internal/state")
            if result is None:
                return self.respond(503, {"error": "Host agent unavailable"})
            return self.respond(200, {"Id": "Host", **result, "Actions": {"#ComputerSystem.Reset": {"target": HOST_RESET}}})
        if self.path == ETH:
            with model.lock:
                return self.respond(200, {"Id": "eth0", "IPv4": dict(model.network), "LinkStatus": "Up"})
        if self.path == EVENTS:
            log = model.logs()
            return self.respond(200, {"Members@odata.count": len(log), "Members": log})
        return self.respond(404, {"error": "Unknown resource"})

    def do_PATCH(self):
        if not self.authorize(write=True):
            return
        if self.path != ETH:
            return self.respond(404, {"error": "Unknown resource"})
        try:
            body = self.read_json()
            updated = self.server.model.set_network(body)
        except ValueError as error:
            return self.respond(400, {"error": str(error)})
        self.respond(200, {"Id": "eth0", "IPv4": updated})

    def do_POST(self):
        if not self.authorize(write=True):
            return
        if self.path == "/_lab/reset":
            if self.agent("/internal/fixture-reset", {}) is None:
                return self.respond(503, {"error": "Host agent unavailable"})
            self.server.model = Model()
            return self.respond(200, {"status": "reset"})
        if self.path not in {HOST_RESET, BMC_RESET}:
            return self.respond(404, {"error": "Unknown action"})
        try:
            body = self.read_json()
            if set(body) != {"ResetType"} or not isinstance(body["ResetType"], str):
                raise ValueError("ResetType must be a string")
            reset = body["ResetType"]
            if self.path == HOST_RESET and reset not in {"ForceRestart", "ForceOff", "On"}:
                raise ValueError("Unsupported host reset type")
            if self.path == BMC_RESET and reset != "GracefulRestart":
                raise ValueError("Unsupported BMC reset type")
        except ValueError as error:
            return self.respond(400, {"error": str(error)})
        if self.path == HOST_RESET:
            response = self.agent("/internal/reset", {"ResetType": reset})
            if response is None:
                return self.respond(503, {"error": "Host agent unavailable"})
            self.server.model.event("Host reset action: " + reset)
            return self.respond(200, response)
        with self.server.model.lock:
            self.server.model.generation += 1
            self.server.model.event("BMC management service restarted")
            generation = self.server.model.generation
        return self.respond(200, {"Generation": generation, "Status": "OK"})


class ManagementServer(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, addr, certfile, keyfile, agent_url, agent_key, admin_token, viewer_token):
        super().__init__(addr, ManagementHandler)
        self.model = Model()
        self.agent_url = agent_url.rstrip("/")
        self.agent_key = agent_key
        self.admin_token = admin_token
        self.viewer_token = viewer_token
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(certfile, keyfile)
        self.socket = context.wrap_socket(self.socket, server_side=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--agent-url", default="http://127.0.0.1:8081")
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()
    agent_key = os.environ.get("HOST_AGENT_KEY", "")
    admin = os.environ.get("BMC_ADMIN_TOKEN", "")
    viewer = os.environ.get("BMC_VIEWER_TOKEN", "")
    if not all(len(x) >= 12 for x in [agent_key, admin, viewer]) or admin == viewer:
        parser.error("All three distinct secrets must be at least 12 characters")
    srv = ManagementServer((args.bind, args.port), args.cert, args.key, args.agent_url,
                           agent_key, admin, viewer)
    print(f"READY bmc={srv.server_address[0]}:{srv.server_address[1]}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
