def test_ntp_info(http_get):
    assert http_get("/ntp/info")[0] == 200


def test_ntp_conf(http_get):
    assert http_get("/ntp/conf")[0] == 200
