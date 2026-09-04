# Демо: срабатывание каждые 10 секунд.
# Запуск: macro run ex_10s.tcl
# После синхронизации NTP раз в 10 секунд печатается время.

set tick_count 0

proc show {msg} {
    puts [clock]
    puts $msg
}

cron {*/10 * * * * *} {
    set tick_count [+ $tick_count 1]
    show {fired: every 10 seconds}
}
