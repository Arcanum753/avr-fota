# Демо: срабатывание каждую минуту (в 0-ю секунду).
# Запуск: macro run ex_1m.tcl

set tick_count 0

proc show {msg} {
    puts [clock]
    puts $msg
}

cron {0 * * * * *} {
    set tick_count [+ $tick_count 1]
    show {fired: every minute}
}
