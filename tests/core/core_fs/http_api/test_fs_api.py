import os
import time
import urllib.parse

import pytest


def _q(path):
    return urllib.parse.quote(path, safe="")


def _entries(http_get):
    status, text = http_get("/fs/list")
    assert status == 200, (status, text)
    import json
    data = json.loads(text)
    assert isinstance(data.get("entries"), list)
    return data


def _find(entries, path):
    for e in entries["entries"]:
        if e.get("path") == path:
            return e
    return None


@pytest.fixture(autouse=True)
def _only_test_board(device_identity):
    assert device_identity.get("devicename") == "Electronica", device_identity
    assert device_identity.get("deviceserial") == "D003", device_identity


def test_info(http_get):
    status, text = http_get("/fs/info")
    assert status == 200, (status, text)
    import json
    info = json.loads(text)
    assert info["api"] == 1
    assert info["fsType"] == "littlefs"
    for key in ("total", "used", "free", "usedPct", "blockSize", "maxUpload"):
        assert isinstance(info[key], int), (key, info)
    assert 0 <= info["usedPct"] <= 100
    assert info["maxUpload"] > 0


def test_list_schema(http_get):
    data = _entries(http_get)
    assert data["count"] == len(data["entries"])
    assert isinstance(data["truncated"], bool)
    for e in data["entries"]:
        assert e["path"].startswith("/")
        assert e["type"] in ("file", "dir")
        assert isinstance(e["size"], int)
        assert isinstance(e["mtime"], int)


def test_crud_roundtrip(http_get, http_post, http_post_raw, http_get_raw):
    name = f"/t_{os.getpid()}_{int(time.time())}"
    renamed = name + "r"
    try:
        status, text = http_post(f"/fs/create?path={_q(name)}")
        assert status == 200, (status, text)
        assert _find(_entries(http_get), name) is not None

        status, text = http_post(f"/fs/rename?from={_q(name)}&to={_q(renamed)}")
        assert status == 200, (status, text)
        assert _find(_entries(http_get), name) is None
        assert _find(_entries(http_get), renamed) is not None

        payload = b"hello-core-fs"
        status, raw = http_post_raw(f"/fs/upload?path={_q(renamed)}", payload)
        assert status == 200, (status, raw)
        e = _find(_entries(http_get), renamed)
        assert e["size"] == len(payload)

        status, raw = http_get_raw(f"/fs/download?path={_q(renamed)}")
        assert status == 200
        assert raw == payload

        status, text = http_post(f"/fs/delete?path={_q(renamed)}")
        assert status == 200, (status, text)
        assert _find(_entries(http_get), renamed) is None
    finally:
        http_post(f"/fs/delete?path={_q(name)}")
        http_post(f"/fs/delete?path={_q(renamed)}")


def test_mkdir_and_recursive_delete(http_get, http_post, http_post_raw):
    d = f"/d_{os.getpid()}_{int(time.time())}"
    f = d + "/a.txt"
    try:
        status, text = http_post(f"/fs/mkdir?path={_q(d)}")
        assert status == 200, (status, text)
        assert _find(_entries(http_get), d)["type"] == "dir"

        status, raw = http_post_raw(f"/fs/upload?path={_q(f)}", b"x")
        assert status == 200, (status, raw)

        status, text = http_post(f"/fs/delete?path={_q(d)}")
        assert status == 409, (status, text)

        status, text = http_post(f"/fs/delete?path={_q(d)}&recursive=1")
        assert status == 200, (status, text)
        assert _find(_entries(http_get), d) is None
    finally:
        http_post(f"/fs/delete?path={_q(d)}&recursive=1")


def test_bad_path(http_post):
    status, _ = http_post(f"/fs/create?path={_q('/a/../evil')}")
    assert status == 400
    status, _ = http_post(f"/fs/create?path={_q('/' + 'x' * 33)}")
    assert status == 400


def test_zero_byte_and_overwrite(http_get_raw, http_post_raw, http_post):
    p = f"/tmp_test_zero_{os.getpid()}.bin"
    try:
        status, _ = http_post_raw(f"/fs/upload?path={_q(p)}", b"")
        assert status == 200
        status, raw = http_get_raw(f"/fs/download?path={_q(p)}")
        assert status == 200 and raw == b""

        status, _ = http_post_raw(f"/fs/upload?path={_q(p)}", b"one")
        assert status == 200
        status, raw = http_get_raw(f"/fs/download?path={_q(p)}")
        assert raw == b"one"

        status, _ = http_post_raw(f"/fs/upload?path={_q(p)}", b"two-two")
        assert status == 200
        status, raw = http_get_raw(f"/fs/download?path={_q(p)}")
        assert raw == b"two-two"
    finally:
        http_post(f"/fs/delete?path={_q(p)}")


def test_secret_is_hidden_but_downloadable(http_get, http_get_raw, http_post):
    data = _entries(http_get)
    if _find(data, "/secret.json") is None:
        pytest.skip("secret.json not present")
    status, _ = http_get("/secret.json")
    assert status == 403
    status, raw = http_get_raw("/fs/download?path=%2Fsecret.json")
    assert status == 200
    assert b"auth" in raw.lower() or raw.strip().startswith(b"{")
    status, _ = http_post("/fs/delete?path=%2Fsecret.json")
    assert status == 403
    status, _ = http_post("/fs/rename?from=%2Fsecret.json&to=%2Fsecret2.json")
    assert status == 403


def test_page_served(http_get):
    status, text = http_get("/filesystem.html")
    assert status == 200
    assert "Filesystem" in text
