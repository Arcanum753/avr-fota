import json


def test_time_info(http_get):
    status, body = http_get("/time/info")
    assert status == 200
    assert "time_now_str|" in body
    assert "time_valid|" in body
    assert "time_sources|" in body


def test_time_sources(http_get):
    status, body = http_get("/time/sources")
    assert status == 200
    data = json.loads(body)
    assert isinstance(data, list)
    for src in data:
        assert "name" in src
        assert "prio" in src
        assert "enabled" in src
        assert "valid" in src
        assert "status" in src


def test_time_state_catalog(http_get):
    status, body = http_get("/state/catalog")
    assert status == 200
    data = json.loads(body)
    names = {r.get("full") for r in data["resources"]}
    for full in ("time.now", "time.now_str", "time.hour", "time.minute",
                 "time.second", "time.valid", "time.source", "time.sources",
                 "time.tz", "time.dst"):
        assert full in names, full


def test_time_save_and_tz(http_get, http_post):
    status, _ = http_post("/time/save", data={"tz": "45", "syncIntervalS": "0"})
    assert status == 200
    status, body = http_get("/time/info")
    assert status == 200
    assert "time_tz|45|" in body


def test_time_set(http_post):
    status, body = http_post("/time/set", data={"value": "1712345678"})
    assert status == 200


def test_time_sync_unknown_source(http_post):
    status, _ = http_post("/time/sync", data={"source": "no_such"})
    assert status == 200
