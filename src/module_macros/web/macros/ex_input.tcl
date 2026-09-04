# Демо: правила ввода с параметрами из терминала.
# Примеры:
#   macro run ex_input.tcl
#   macro msg temp 25      - сработает term {temp 25}
#   macro msg button1 on   - сработает term {button1 on}
#   macro btn start        - сработает button {start}
# При совпадении параметров модуль печатает «условие ... сработало»
# и выполняет тело правила в интерпретаторе этого файла.

proc report {msg} {
    puts [clock]
    puts $msg
}

term {temp 25} {
    report {sensor: temperature 25}
}

term {button1 on} {
    report {button1 is ON}
}

button {start} {
    report {button start pressed}
}
