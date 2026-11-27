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
TEST_DEVICE_HOST = "http://192.168.88.134"
TEST_DEVICE_NAME = "Electronica"
TEST_DEVICE_SERIAL = "D003"


def _headers():
    token = base64.b64encode(f"{DEVICE_USER}:{DEVICE_PASS}".encode()).decode()
    return {"Authorization": "Basic " + token}


def _request(path, method="GET", data=None, timeout=60):
    url = DEVICE_HOST.rstrip("/") + path
    body = data.encode() if isinstance(data, str) else data
    req = urllib.request.Request(url, data=body, headers=_headers(), method=method)
    for attempt in (1, 2, 3):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                data = resp.read().decode(errors="replace")
            time.sleep(0.2)
            return resp.status, data
        except urllib.error.HTTPError as e:
            time.sleep(0.2)
            return e.code, e.read().decode(errors="replace")
        except (TimeoutError, urllib.error.URLError):
            if attempt == 3:
                raise
            time.sleep(3)


def _request_raw(path, method="GET", data=None, timeout=60):
    url = DEVICE_HOST.rstrip("/") + path
    body = data.encode() if isinstance(data, str) else data
    headers = _headers()
    if body is not None:
        headers["Content-Type"] = "application/octet-stream"
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
        time.sleep(0.2)
        return resp.status, data
    except urllib.error.HTTPError as e:
        time.sleep(0.2)
        return e.code, e.read()


@pytest.fixture(autouse=True)
def _require_device():
    if not DEVICE_HOST:
        pytest.skip("DEVICE_HOST not set")
    if os.environ.get("ALLOW_ANY_DEVICE") != "1" and DEVICE_HOST.rstrip("/") != TEST_DEVICE_HOST:
        pytest.skip(f"only test device {TEST_DEVICE_HOST} is allowed")


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
def http_get_raw():
    return lambda path, **kw: _request_raw(path, "GET", **kw)


@pytest.fixture
def http_post_raw():
    return lambda path, data=None, **kw: _request_raw(path, "POST", data, **kw)


@pytest.fixture
def device_identity(http_get):
    """CVT /system/version -> dict; guard тестового устройства."""
    status, text = http_get("/system/version")
    assert status == 200, (status, text)
    out = {}
    for line in text.splitlines():
        parts = line.split("|")
        if len(parts) >= 2:
            out[parts[0].strip()] = parts[1]
    return out


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
