# core_terminal — последовательный терминал

`core_terminal` отвечает только за ввод/вывод, справку и подтверждения. Команды
регистрируют и обрабатывают сами модули (статический метод в `<module>_terminal.cpp`).

## API (`core_terminal.h`)

- `TerminalRegisterModule(component, name, desc, handler, commands, helpText, editable)` —
  регистрирует команду верхнего уровня (`name`) и дескриптор для `help`. Вызывается из
  `begin()` модуля; `term.addCommand()` выполняется сразу (порядок begin не важен).
  `component` — имя компонента (`core_sys`), `name` — команда (`sys`).
- `TerminalRegisterAlias(name, handler)` — скрытая команда/алиас (в `help` не попадает).
- `TerminalHasModule(component)` — есть ли у компонента терминальные команды (для `stat`).
- `TerminalConfirm(prompt, onYes)` — подтверждение `y/n` с таймаутом 30 с; любая другая
  команда отменяет ожидание. `TerminalCancelConfirm()` — отменить программно.
- `TerminalPrintHelp()`, `TerminalPrintModuleHelp(name)`, `TerminalPrintText(text)` —
  справка и печать PROGMEM-текста (ESP8266).
- `TerminalGetArgLine()` — остаток строки после `term.getNext()`, trim + снятие кавычек.

## Контракт модуля

1. В `begin()` вызвать `terminalRegister()`.
2. В `<module>_terminal.cpp` реализовать `terminalRegister()` и `s_terminalCmd()`.
3. `s_terminalCmd()` — статический метод класса (доступ к приватным полям через
   `core_xxx.field`); `term.getNext()` — подкоманда, `TerminalGetArgLine()` — значение.
4. Подкоманда `help` (и вызов без аргументов) печатает статический `helpText` через
   `TerminalPrintText()`.
5. Помечать `helpText` как `PROGMEM` (экономия DRAM ESP8266).
6. Регистрировать `component` в дескрипторе — `stat` использует его для флага `term`.

## Ограничения

- Буфер строки — 320 байт (`ST_RX_BUFFER_SIZE`); имя команды — до 8 символов.
- Дескрипторы модулей в heap, лимит `TERMINAL_MAX_MODULES = 24`.
- Встроенные `help`/`echo` и скрытые `y`/`n` регистрирует `TerminalInit()`.
- Не добавлять в `core_terminal` команды модулей и include соседних `module_*`.
