import json


def test_state_catalog(http_get):
    status, body = http_get("/state/catalog")
    assert status == 200
    data = json.loads(body)
    assert "resources" in data


def test_state_info_known(http_get):
    status, body = http_get("/state/info?name=system.mode")
    assert status == 200
    assert "name|" in body


def test_state_info_unknown(http_get):
    status, _ = http_get("/state/info?name=no.such.resource")
    assert status == 404


def test_state_call_unknown_error_code(http_get):
    status, body = http_get("/state/call?name=no.such.func")
    assert status == 200
    assert body.strip() == "ERR_NOT_FOUND"


def test_state_set_readonly_or_notfound(http_get):
    status, body = http_get("/state/set?name=no.such.resource&value=1")
    assert status == 404
