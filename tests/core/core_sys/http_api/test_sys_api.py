def test_system_version(http_get):
    status, body = http_get("/system/version")
    assert status == 200
    assert "|" in body


def test_system_infovalues(http_get):
    status, body = http_get("/system/infovalues")
    assert status == 200
    assert len(body) > 0


def test_recover_pages(http_get):
    assert http_get("/recover")[0] == 200
    assert http_get("/recover/status")[0] == 200


def test_system_restart(http_post, wait_online):
    status, _ = http_post("/system/restart")
    assert status in (200, 204)
    assert wait_online(timeout=60)
