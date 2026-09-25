-- WiFi: расписание цели — днём AP, вечером STA.
-- Требует режима wifi.mode = macro (иначе set("wifi.target") игнорируется).
-- Cron срабатывает только после синхронизации NTP.
return {
    desc = "WiFi schedule: AP 10:00, STA 22:00",
    rules = {
        { when = { cron = "0 0 10 * * *" }, set = "wifi.target", value = 1 },  -- 10:00 -> AP
        { when = { cron = "0 0 22 * * *" }, set = "wifi.target", value = 2 },  -- 22:00 -> STA
    },
}
