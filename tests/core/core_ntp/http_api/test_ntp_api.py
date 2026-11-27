def test_ntp_info(http_get):
    status, body = http_get("/ntp/info")
    assert status == 200
    # Данные отдаются по кэшу последнего синка (а не по SyncStatus()).
    assert "x_ntp_sync|" in body
    assert "x_ntp_time|" in body
    assert "x_ntp_adr|" in body


def test_ntp_conf(http_get):
    status, body = http_get("/ntp/conf")
    assert status == 200
    assert "ntpserver0|" in body
    assert "update|" in body
