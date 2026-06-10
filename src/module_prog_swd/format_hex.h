#ifndef _FORMAT_HEX_H
#define _FORMAT_HEX_H

#include <Arduino.h>
#include <FS.h>
#include <vector>

/**
 * @file format_hex.h
 * @brief Свободные функции для работы с HEX-файлами прошивки (Intel HEX).
 *
 * Предоставляет единый интерфейс потокового парсинга/валидации HEX-файлов.
 * Аналог format_bin.h для HEX-формата.
 */

/**
 * @brief Парсинг одной строки Intel HEX формата :LLAAAATT[DD...]CC.
 *
 * Разбирает строку, начиная с позиции beginLine в строке lineStr.
 * Извлекает длину, адрес, тип записи, данные и контрольную сумму.
 *
 * @param lineStr     Строка Intel HEX (начинается с ':').
 * @param pageaddr    Выход: адрес из строки (16 бит).
 * @param lineBuf     Выход: буфер с бинарными данными строки (должен быть >= 256 байт).
 * @param chsum       Выход: вычисленная контрольная сумма (0 если OK).
 * @param type        Выход: тип записи (0 = data, 1 = end of file).
 * @param binReadNum  Выход: количество бинарных байт в строке.
 * @return true если строка успешно распарсена, false при ошибке.
 */
bool hexFileLineParser(const String &lineStr,
                       uint16_t &pageaddr, uint8_t *lineBuf,
                       uint8_t &chsum, uint8_t &type,
                       uint8_t &binReadNum);

/**
 * @brief Тип callback для потоковой записи HEX в flash.
 *
 * Вызывается для каждого непрерывного чанка бинарных данных.
 *
 * @param chunkAddr  Адрес в flash, куда нужно писать данные.
 * @param data       Указатель на данные.
 * @param size       Размер данных в байтах.
 * @param userData   Произвольный указатель (например, на ESP_PROGSWD).
 * @return 0 при успехе, -1 при ошибке (прерывает парсинг).
 */
typedef int (*hex_write_callback_t)(uint32_t chunkAddr, const uint8_t *data, uint32_t size, void *userData);

/**
 * @brief Потоковый парсинг и валидация HEX-файла.
 *
 * Читает файл построчно из File-объекта, выполняет полную валидацию:
 * - Парсинг каждой строки через hexFileLineParser()
 * - Проверка монотонности адресов
 * - Проверка контрольной суммы каждой строки
 * - Проверка переполнения памяти чипа
 * - Сборка бинарных данных в binDataBuf
 *
 * НЕ загружает весь HEX-файл в RAM — читает потоково.
 *
 * @param file        Открытый File-объект для чтения.
 * @param binDataBuf  Выходной буфер с бинарными данными прошивки.
 * @param flashStartAddr Начальный адрес flash-памяти (например, 0x08000000 для STM32, 0 для AVR).
 * @param chipMemSize    Размер памяти чипа в байтах (для проверки переполнения).
 * @param totalBins      Выход: общее количество бинарных байт.
 * @return >=0 количество бинарных байт при успехе,
 *         <0 код ошибки (-9 ERR_INCORRECTFILE, -11 ERR_HEXCRC, -12 ERR_HEXMEMOVER, -13 ERR_HEXADDR).
 */
int32_t hexFileParseStream(File &file, std::vector<char> &binDataBuf,
                           uint32_t flashStartAddr, uint32_t chipMemSize, uint32_t &totalBins);

/**
 * @brief Потоковый парсинг HEX-файла с immediate-записью через callback.
 *
 * Аналог hexFileParseStream(), но вместо накопления данных в буфер
 * вызывает writeCallback для каждого непрерывного чанка данных.
 * Это позволяет писать данные напрямую в flash без хранения всего
 * бинарного образа в RAM.
 *
 * @param file        Открытый File-объект для чтения.
 * @param flashStartAddr Начальный адрес flash-памяти.
 * @param chipMemSize    Размер памяти чипа в байтах.
 * @param pageSize       Размер страницы flash (chunkBuf будет равен pageSize, но не более 1024).
 * @param writeCallback  Callback для записи чанка данных.
 * @param userData       Произвольный указатель для callback.
 * @return >=0 количество записанных бинарных байт при успехе,
 *         <0 код ошибки (см. hexFileParseStream).
 */
int32_t hexFileParseStreamWrite(File &file, uint32_t flashStartAddr, uint32_t chipMemSize,
                                uint32_t pageSize,
                                hex_write_callback_t writeCallback, void *userData);

/**
 * @brief Проверить, является ли файл HEX-форматом (по расширению).
 * @param path Имя или путь к файлу.
 * @return true если расширение .hex.
 */
bool hexFileIsFormat(const String &path);

#endif // _FORMAT_HEX_H
