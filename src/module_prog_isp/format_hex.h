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
 * @brief Проверить, является ли файл HEX-форматом (по расширению).
 * @param path Имя или путь к файлу.
 * @return true если расширение .hex.
 */
bool hexFileIsFormat(const String &path);

/**
 * @brief Быстрый подсчёт бинарного размера HEX-файла (без парсинга в буфер).
 *
 * Читает файл построчно, подсчитывает суммарное количество бинарных байт
 * во всех data-записях (тип 00). Не загружает данные в RAM.
 * Используется для корректного расчёта процента прошивки.
 *
 * @param file Открытый File-объект для чтения.
 * @return Количество бинарных байт в HEX-файле, или -1 при ошибке.
 */
int32_t hexFileGetBinarySize(File &file);

#endif // _FORMAT_HEX_H
