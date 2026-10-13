"""Tiny TLS-validated QA client used by functional and security test cases."""
from __future__ import annotations

import json
import ssl
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class BmcClient:
    def __init__(self, base_url, ca_cert, token):
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.context = ssl.create_default_context(cafile=str(ca_cert))

    def request(self, method, resource, data=None, token=None, content_type="application/json"):
        headers = {}
        key = self.token if token is None else token
        if key:
            headers["Authorization"] = "Bearer " + key
        payload = None
        if data is not None:
            payload = data if isinstance(data, bytes) else json.dumps(data).encode()
            headers["Content-Type"] = content_type
        request = Request(self.base_url + resource, data=payload, method=method, headers=headers)
        try:
            with urlopen(request, context=self.context, timeout=3) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            try:
                return error.code, json.load(error)
            finally:
                error.close()

    def get(self, path, **kwargs):
        return self.request("GET", path, **kwargs)

    def patch(self, path, body, **kwargs):
        return self.request("PATCH", path, body, **kwargs)

    def post(self, path, body, **kwargs):
        return self.request("POST", path, body, **kwargs)
