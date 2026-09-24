def test_update_progress(http_get):
    assert http_get("/update/progress")[0] == 200


def test_firmware_file_check(http_get):
    status, _ = http_get("/update/firmwarefilecheck?filename=testenv-FIRMWARE-1.002.20260101_1200.0100.bin")
    assert status == 200


def test_setmd5(http_get):
    status, _ = http_get("/update/setmd5?md5=0123456789abcdef0123456789abcdef")
    assert status == 200
