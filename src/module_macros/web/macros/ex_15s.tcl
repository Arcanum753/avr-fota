# Демо: срабатывание каждые 15 секунд.
# Запуск: macro run ex_15s.tcl

set tick_count 0

proc show {msg} {
    puts [clock]
    puts $msg
}

cron {*/15 * * * * *} {
    set tick_count [+ $tick_count 1]
    show {fired: every 15 seconds}
}
