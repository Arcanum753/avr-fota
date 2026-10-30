# core_fs — файловая система (страница Filesystem + HTTP API `/fs/*`)

## Назначение

Ядерный always-on модуль. Даёт страницу **Filesystem** и HTTP API для просмотра
состояния LittleFS и файловых операций **без редактора содержимого**.
Потребитель API — `otasyncer-server` (версионные снимки настроек).

## Состав

- `core_fs.cpp` — шаблонный блок: глобальный объект, `setFs`, `begin(ctx)`,
  `web_Init`, веб-обработчики, версия.
- `core_fs_engine.cpp/.h` — рекурсивный список, info, create/mkdir/rename/delete.
- `common_module.cpp/.h` — `ns_core_fs`: нормализация/проверка путей, secret, режим.
- `core_fs_types.h` — лимиты (`CORE_FS_API_VERSION=1`, `MAX_UPLOAD=1 MiB`,
  `FREE_RESERVE=4 KiB`, `MAX_ENTRIES=256`, `MAX_NAME=32`, `MAX_PATH=255`,
  `MAX_DEPTH=8`, `BLOCK_SIZE=4096`).
- `web/filesystem.html` — страница; пункт меню добавляется в
  `python/gen_page_head.py` (`STATIC_MENU_LINKS`).

Модуль **не** регистрирует ресурсы в `core_state` (`res = 0`) и **не** доступен
из макросов/Lua/`resources.json`: это только HTTP-интерфейс.

## API (все под `checkAuth`)

| Метод | URL | Ответ |
|---|---|---|
| GET | `/fs/info` | JSON `{api,total,used,free,usedPct,blockSize,maxUpload,fsVersion,buildEnv,fsType}` |
| GET | `/fs/list` | JSON `{count,truncated,entries:[{path,type,size,mtime}]}` |
| GET | `/fs/download?path=/...` | raw bytes (`secret.json` тоже, в обход HIDE_SECRET) |
| POST | `/fs/create?path=/...` | пустой файл |
| POST | `/fs/mkdir?path=/...` | каталог (один уровень) |
| POST | `/fs/rename?from=/...&to=/...` | переименование |
| POST | `/fs/delete?path=/...&recursive=0\|1` | удаление файла/каталога |
| POST | `/fs/upload?path=/...` | raw body, overwrite; `Content-Type: application/octet-stream` |
| GET | `/fs/ver` | CVT версии |

- `/fs/upload` принимает только поток тела; `application/x-www-form-urlencoded`
  не обрабатывается — клиент обязан ставить `application/octet-stream`.
- Лимиты: имя сегмента ≤ 32 (LittleFS), upload ≤ 1 MiB и не больше свободного места
  минус 4 KiB; одновременно один upload.
- Ошибки: `400` путь/аргументы, `403` secret/запрет, `404` нет файла, `409`
  существует/занято/не пусто, `413` размер, `500` IO, `507` нет места.
- Записи запрещены в режимах `ota`, `fs_update`, `prog` (`core_state.getMode()`),
  чтение — всегда.
- `/secret.json` виден в списке; `delete`/`rename` запрещены; `download`/`upload`
  разрешены для восстановления.

## Интеграция

- `python/module_registry_gen.py`: `core_fs.h` в includes, `core_fs.begin(ctx)`,
  `core_fs.web_Init()`.
- `python/gen_page_head.py`: ссылка `Filesystem`.
- Тесты: `tests/core/core_fs/` (L1/L2 native, L4 `http_api/`).
  L4 — только устройство `http://192.168.88.134`, имя `Electronica`, серийник `D003`;
  destructive — только временные файлы.
