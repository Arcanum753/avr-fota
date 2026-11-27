-- WiFi: скан-серия и отключение STA.
-- wifi scan -> force_scan(10): 10 сканов подряд, выбрать лучшую настроенную сеть
-- wifi disc -> force_disconnect: отключить STA (AP не трогаем)
return {
    desc = "WiFi scan/disconnect: term 'wifi scan'/'wifi disc'",
    rules = {
        { when = { term = "wifi scan" }, call = "wifi.force_scan", args = { 10 } },
        { when = { term = "wifi disc" }, call = "wifi.force_disconnect" },
    },
}
