-- WiFi: force-команды по слову из терминала (macro msg ...).
-- wifi ap    -> безопасно в AP (если есть клиент AP — no-op BUS_ERR_BUSY)
-- wifi kick  -> в AP, выгнав всех клиентов
return {
    desc = "WiFi force: term 'wifi ap'/'wifi kick'",
    rules = {
        { when = { term = "wifi ap" },   call = "wifi.force_ap" },
        { when = { term = "wifi kick" }, call = "wifi.force_ap_kick" },
    },
}
