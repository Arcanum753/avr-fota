def test_wifi_info(http_get):
    assert http_get("/wifi/info")[0] == 200


def test_wifi_scan(http_get):
    assert http_get("/wifi/scan")[0] == 200


def test_wifi_slot(http_get):
    assert http_get("/api/wifi/slot/0")[0] == 200


def test_captive_portal(http_get):
    for path in ("/generate_204", "/hotspot-detect.html", "/ncsi.txt"):
        assert http_get(path)[0] == 200, path
