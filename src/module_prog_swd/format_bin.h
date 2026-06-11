#ifndef _FORMAT_BIN_H
#define _FORMAT_BIN_H

#include <Arduino.h>
#include <FS.h>

/**
 * @file format_bin.h
 * @brief Свободные функции для работы с BIN-файлами прошивки.
 *
 * Предоставляет единый интерфейс открытия/чтения/закрытия BIN-файлов.
 * В будущем аналогичный интерфейс может быть реализован для HEX-формата
 * (format_hex.h), что позволит унифицировать работу с разными форматами
 * прошивок на уровне программатора.
 */

/**
 * @brief Открыть BIN-файл из файловой системы.
 * @param fs   Ссылка на объект файловой системы (SPIFFS).
 * @param path Путь к файлу (может быть без ведущего '/' — добавится автоматически).
 * @return File-объект. Проверка успеха: if (!file) возвращает "пустой" File.
 */
File binFileOpen(fs::FS &fs, const String &path);

/**
 * @brief Получить размер открытого BIN-файла.
 * @param file Ссылка на открытый File-объект.
 * @return Размер файла в байтах.
 * @note Файл должен быть открыт. После вызова позиция чтения сбрасывается в начало.
 */
uint32_t binFileGetSize(File &file);

/**
 * @brief Прочитать одну страницу (порцию) данных из BIN-файла.
 * @param file       Ссылка на открытый File-объект.
 * @param buffer     Буфер для данных (должен быть не меньше bufferSize).
 * @param bufferSize Размер буфера / максимальное количество байт для чтения.
 * @return Количество реально прочитанных байт (может быть меньше bufferSize
 *         в конце файла или при ошибке).
 */
uint32_t binFileReadPage(File &file, uint8_t *buffer, uint32_t bufferSize);

/**
 * @brief Закрыть BIN-файл.
 * @param file Ссылка на открытый File-объект.
 */
void binFileClose(File &file);

/**
 * @brief Проверить, является ли файл BIN-форматом (по расширению).
 * @param path Имя или путь к файлу.
 * @return true если расширение .bin или .binary.
 */
bool binFileIsFormat(const String &path);

#endif // _FORMAT_BIN_H
