-- WiFi: реакция на события AP (подписка на шину).
-- При подключении/отключении клиента AP печатает в терминал [MACRO].
return {
    desc = "WiFi AP events: on ap_client_joined/left",
    handlers = {
        onjoin = function(ev) print("ap client joined: " .. tostring(ev.spec)) end,
        onleft = function(ev) print("ap client left: " .. tostring(ev.spec)) end,
    },
    rules = {
        { when = { on = "wifi.ap_client_joined" }, run = "onjoin" },
        { when = { on = "wifi.ap_client_left" },   run = "onleft" },
    },
}
