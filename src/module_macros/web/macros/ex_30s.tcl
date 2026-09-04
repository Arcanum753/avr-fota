# Демо: срабатывание каждые 30 секунд.
# Запуск: macro run ex_30s.tcl

set tick_count 0

proc show {msg} {
    puts [clock]
    puts $msg
}

cron {*/30 * * * * *} {
    set tick_count [+ $tick_count 1]
    show {fired: every 30 seconds}
}
