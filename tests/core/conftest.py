# tests/core/conftest.py
# Общие фикстуры L4 (HTTP API) для всех модулей ядра.
# Без DEVICE_HOST тесты автоматически пропускаются (exit 0).

import base64
import json
import os
import time
import urllib.error
import urllib.request

import pytest

DEVICE_HOST = os.environ.get("DEVICE_HOST", "")
DEVICE_USER = os.environ.get("DEVICE_USER", "admin")
DEVICE_PASS = os.environ.get("DEVICE_PASS", "")


def _headers():
    token = base64.b64encode(f"{DEVICE_USER}:{DEVICE_PASS}".encode()).decode()
    return {"Authorization": "Basic " + token}


def _request(path, method="GET", data=None, timeout=10):
    url = DEVICE_HOST.rstrip("/") + path
    body = data.encode() if isinstance(data, str) else data
    req = urllib.request.Request(url, data=body, headers=_headers(), method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")


@pytest.fixture(autouse=True)
def _require_device():
    if not DEVICE_HOST:
        pytest.skip("DEVICE_HOST not set")


@pytest.fixture
def device_host():
    return DEVICE_HOST


@pytest.fixture
def http_get():
    return lambda path, **kw: _request(path, "GET", **kw)


@pytest.fixture
def http_post():
    return lambda path, data=None, **kw: _request(path, "POST", data, **kw)


@pytest.fixture
def wait_online():
    def _wait(timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            status, _ = _request("/", timeout=3)
            if status in (200, 401):
                return True
            time.sleep(1)
        return False
    return _wait
