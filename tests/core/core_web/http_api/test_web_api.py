import json


def test_all_returns_json(http_get):
    status, body = http_get("/all")
    assert status == 200
    data = json.loads(body)
    assert isinstance(data, dict)


def test_secret_json_hidden(http_get):
    # main.h определяет HIDE_SECRET -> 403 после аутентификации.
    status, _ = http_get("/secret.json")
    assert status in (401, 403)


def test_not_found_fallback(http_get):
    status, _ = http_get("/definitely-missing-page-xyz")
    assert status in (200, 404)
