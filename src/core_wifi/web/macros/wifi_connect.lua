-- WiFi: подключение к конкретному SSID строкой.
-- wifi keenetic -> set_slot("Keenetic-4208") + force_connect
-- wifi pokr     -> set_slot("Pokr8-3") + force_connect
return {
    desc = "WiFi connect: term 'wifi keenetic'/'wifi pokr'",
    rules = {
        { when = { term = "wifi keenetic" }, calls = {
            { name = "wifi.set_slot", args = { "Keenetic-4208" } },
            { name = "wifi.force_connect" },
        } },
        { when = { term = "wifi pokr" }, calls = {
            { name = "wifi.set_slot", args = { "Pokr8-3" } },
            { name = "wifi.force_connect" },
        } },
    },
}
