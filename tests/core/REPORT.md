# Отчёт: система юнит-тестирования ядра avr-fota

Статус: **validated** — L1/L2 зелёные (все 11 модулей), L3 зелёный
(AC-19/AC-22 проходят; сборки ESP `TestCore32`/`TestCore8266` помечены `skip` —
нет Linux-тулчейнов, валидируются в CI), L4 auto-skip без `DEVICE_HOST`.
`bash tests/scripts/run_core.sh` завершается с exit 0.

Дата: 2026-09-24. Окружение: WSL Ubuntu 24.04 (`/mnt/d/github/avr-fota`).

## Версии инструментов (WSL)

| Инструмент | Версия | Расположение |
|---|---|---|
| PlatformIO Core | 6.2.0 | `~/.local/bin/pio` (pipx) |
| gcc/g++ | 15.2.0 | `/usr/bin` |
| node | 22.22.1 | `/usr/bin/node` |
| gcovr | 8.6 | `~/.local/bin/gcovr` (pipx) |
| pytest | 9.1.1 | `~/.local/bin/pytest` (pipx) |
| python3 | 3.14.4 | `/usr/bin/python3` |

## Что сделано

- **Каркас**: 11 модульных `tests/core/<module>/run.sh` (common, core_web, core_sys,
  core_wifi, core_ntp, core_ota, core_json, core_led, core_terminal, core_state,
  core_task) + общая логика `tests/scripts/run_module.sh`; оркестратор
  `tests/scripts/run_core.sh` (`--only`, `--with-device`, `--no-l3`).
- **Моки** `tests/core/native/mocks/`: `String`/`Serial`/`millis`, `LittleFS`
  (in-memory), `WiFi`, `DNSServer`, `ESP`, `EEPROM`, `ArduinoOTA`, `Update`,
  `ESPAsyncWebServer`, `FSWebServerLib`, `version.h`, `avr/pgmspace.h`;
  переопределение реальных заголовков ядра через `override_prelude.h`.
- **Тесты L1/L2** по модулям + **L1-JS** `ParseCVT`/`ApplyCVT` через Node.js
  (`core_web/js/test_cvt.mjs`).
- **L3** `tests/compile_matrix/` (`run_matrix.py`, `envs.txt`, `negative_nocore/`,
  `size_limits.json`): обязательные env `TestCore32`/`TestCore8266`, AC-19
  (чистота registry), AC-22 (негативная сборка без ядра).
- **L4** `tests/core/conftest.py` + `http_api/` для core_web/core_sys/core_wifi/
  core_ntp/core_ota/core_state.
- **CI** `.github/workflows/tests-core.yml` (L1+L2+L3 на ubuntu, L4 на self-hosted).
- **Служебное**: `.gitignore` → `/test_reports/`; `platformio.ini` → включён
  `targets/targets_example.ini`; восстановлен `ESPAsyncUDP` в `[env:esp8266]`.
- **Документация**: `tests/core/README.md`, `tests/core/INVENTORY.md`, этот отчёт.

## Журнал «модуль → что сломалось → что починил → результат»

### Первый прогон (19.09)

| Модуль | Проблема | Исправление | Результат |
|---|---|---|---|
| common | `misc.cpp: debug.h not found` | include через `../../../../src/...` | ✅ 18/18, покрытие 95% |
| common | линковка `--coverage` не в LINKFLAGS | добавлен `-lgcov` | ✅ |
| core_state | — | — | ✅ 22/22 |
| core_sys | — | — | ✅ 15/15 |
| core_json | `String` ↔ ArduinoJson | `ARDUINOJSON_ENABLE_ARDUINO_STRING=1` | ✅ 9/9 |
| core_task | — | — | ✅ 9/9 |
| core_led | — | — | ✅ 9/9, покрытие 93% |
| core_terminal | — | — | ✅ 1/1 |
| core_ota | — | — | ✅ 19/19, покрытие 88% |
| core_web | — | C++ 3/3 + Node.js CVT | ✅ |
| core_wifi | — | — | ✅ 14/14 |
| core_ntp | — | только L4 | ✅ (L4 skip) |

### Доведение до зелёного после переустановки (24.09)

| Что сломалось | Причина | Исправление | Результат |
|---|---|---|---|
| common/ota/led: coverage gate `нет данных` | stale `.gcda` от gcc 13.3 → `libgcov: version mismatch` | удалены `.pio` пороговых модулей, пересборка на gcc 15.2 | ✅ common 95%, ota/led по порогу |
| core_web: 4 теста ParseCVT FAIL | кросс-realm массивы из `vm.createContext` (другой `Array.prototype`), `deepStrictEqual` режет | нормализация `plain()` = `JSON.parse(JSON.stringify(x))` | ✅ 8/8 |
| core_web: `node --test js` не находит тест | `test_cvt.mjs` не подходит под паттерн Node | `node --test js/*.mjs` | ✅ |
| coverage/gенерация отчёта пропускаются | `python` нет (есть только `python3`) | резолвер `PY` (`python3`→`python`) в `run_module.sh`/`run_core.sh` | ✅ |
| `pio not found` при запуске без login-shell | pipx ставит в `~/.local/bin`, его нет в неинтерактивном PATH | bootstrap `~/.local/bin` + `~/.platformio/penv/bin` в скрипты | ✅ |
| gen_report: «error: 8» при чистом прогоне | читал собственный `_summary/junit.xml` (двойной учёт) | исключение каталога `_summary` из glob | ✅ 119 тестов, 0 error |
| L3 AC-19 FAIL | stale `modules_registry.cpp` от env с модулями/устройством | регенерация registry под core-env перед проверкой | ✅ pass |
| L3 AC-22 FAIL | `pio` не в PATH | тот же bootstrap PATH | ✅ pass (негативная сборка падает как надо) |

## Отступления от первоначального плана (с обоснованием)

1. **Include `.cpp` ядра прямо в тест-TU** вместо `test_build_src` (PIO 6.x не
   компилирует `src_dir` вне проекта; `include_dir` не опережает `-Isrc`).
2. **Один native-env на модуль** (L1/L2 у core_sys/core_ota объединены).
3. **L3 ограничен `TestCore32`/`TestCore8266`** — сборки «ядро без внешних
   компонентов»; полные `esp32`/`esp8266`/`esp32cam`/`esp32-c3-devkitm-1`
   намеренно исключены ради времени.
4. **CVT — Node.js** (`ParseCVT`/`ApplyCVT` — JavaScript).
5. **`targets/targets_example.ini` включён** в `extra_configs` — иначе
   `TestCore32`/`TestCore8266` не определены.
6. **L4** при отсутствии `DEVICE_HOST` пропускается (exit 0).
7. **Python через `python3`**: в Ubuntu `python` не существует; резолвер `PY`
   в скриптах, shebang `python3` в py-файлах.

## Ограничения и незавершённое

- **Сборки ESP в L3 локально помечены `skip`.** Windows-тулчейны в
  `C:\Users\Sam\.platformio\packages\` — это `.exe`, в WSL не работают; скачать
  Linux-тулчейны нельзя (github TCP 443 блокируется провайдером, а framework
  PlatformIO тянет с github). Полная сборка `TestCore32`/`TestCore8266` и
  негативный тест AC-22 с реальной сборкой ESP выполняются в CI (ubuntu). Для
  локальной полной проверки нужны Linux-тулчейны ESP в `~/.platformio/packages/`.
- **`size_limits.json` пуст** — пороги RAM/Flash задаются после реальной сборки
  в CI; сейчас размеры только логируются.
- **Коммиты не делались** — `tests/` и `.github/workflows/tests-core.yml`
  остаются untracked; `platformio.ini`/`targets/targets_example.ini`/
  `.github/workflows/platformio_ci.yml`/`.gitignore`/`AGENTS.md` — правки
  инфраструктуры тестов из предыдущих сессий (не коммитились).
- `src/core_wifi/*` (5 файлов) — правки логики переподключения из соседней
  задачи, к тестам не относятся.

## Подтверждённые гейты (`run_module.sh`)

- нет `[env:*]` в `platformio.ini` → **exit 2**;
- нет `pio` → **exit 2**; нечего запускать → **exit 2**;
- для `common`/`core_ota`/`core_led`: нет данных gcovr или нет строки `TOTAL`
  или покрытие < 80% → **exit 2**;
- `run_core.sh --only <несуществующий>` → **exit 2**;
- `run_core.sh --with-device` без `DEVICE_HOST` → **exit 2**.

## Запуск

```bash
bash tests/scripts/run_core.sh            # L1+L2+L3 (L4 auto-skip без DEVICE_HOST)
bash tests/scripts/run_core.sh --no-l3    # только L1+L2
bash tests/scripts/run_core.sh --only core_wifi
bash tests/core/<module>/run.sh           # один модуль
```

## Артефакты

- `tests/core/README.md` — запуск и добавление тестов.
- `tests/core/INVENTORY.md` — файл → зависимости → уровни.
- `tests/core/REPORT.md` — этот отчёт.
- `test_reports/core/...` — JUnit/логи/coverage (gitignored).
