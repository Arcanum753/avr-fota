#include "format_hex.h"
#include "common.h"  // для hex2bin()

/**
 * @file format_hex.cpp
 * @brief Реализация свободных функций для работы с HEX-файлами (Intel HEX).
 *
 * Потоковый парсинг — не загружает весь HEX-файл в RAM.
 */

// Символ начала строки Intel HEX
#define HEX_PARSE_LINEBEGIN      ':'

/**
 * @brief Парсинг одной строки Intel HEX формата :LLAAAATT[DD...]CC.
 *
 * Разбирает строку lineStr (начинается с ':').
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
                       uint8_t &binReadNum) {
    // Проверка, что строка начинается с ':'
    if (lineStr.length() < 11 || lineStr[0] != ':') {
        return false;
    }

    uint32_t pos = 1; // позиция после ':'
    uint8_t len;
    uint8_t b = 0;
    chsum = 0;

    // Читаем длину данных (1 байт = 2 hex-символа)
    len =            hex2bin(lineStr[pos++]);
    len = (len << 4) + hex2bin(lineStr[pos++]);
    chsum = len;

    // Проверка минимальной длины строки: 1 (длина) + 2 (адрес) + 1 (тип) + len (данные) + 1 (CRC) = 5 + len байт
    // Каждый байт = 2 hex-символа, итого 10 + len*2 символов + ':' = 11 + len*2
    if (lineStr.length() < (uint32_t)(11 + len * 2)) {
        return false;
    }

    // Читаем старший байт адреса
    b =            hex2bin(lineStr[pos++]);
    b = (b << 4) + hex2bin(lineStr[pos++]);
    chsum += b;
    pageaddr = (uint16_t)b << 8;

    // Читаем младший байт адреса
    b =            hex2bin(lineStr[pos++]);
    b = (b << 4) + hex2bin(lineStr[pos++]);
    chsum += b;
    pageaddr |= b;

    // Читаем тип записи
    b =            hex2bin(lineStr[pos++]);
    b = (b << 4) + hex2bin(lineStr[pos++]);
    chsum += b;
    type = b;

    // Читаем данные
    binReadNum = 0;
    for (uint8_t i = 0; i < len; i++) {
        b =            hex2bin(lineStr[pos++]);
        b = (b << 4) + hex2bin(lineStr[pos++]);
        lineBuf[i] = b;
        chsum += b;
        binReadNum++;
    }

    // Читаем контрольную сумму
    b =            hex2bin(lineStr[pos++]);
    b = (b << 4) + hex2bin(lineStr[pos++]);
    chsum += b;

    return true;
}

/**
 * @brief Потоковый парсинг и валидация HEX-файла.
 *
 * Читает файл построчно из File-объекта, выполняет полную валидацию:
 * - Парсинг каждой строки через hexFileLineParser()
 * - Проверка монотонности адресов
 * - Проверка контрольной суммы каждой строки
 * - Проверка переполнения памяти чипа (по реальному адресу относительно flashStartAddr)
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
                           uint32_t flashStartAddr, uint32_t chipMemSize, uint32_t &totalBins) {
    if (!file) {
        return -10; // ERR_NOFILE
    }

    // Очищаем выходной буфер
    binDataBuf.clear();
    // Не резервируем память заранее — вектор будет расти динамически.
    // reserve(chipMemSize) для больших чипов (512 КБ у STM32F4) вызывает abort() на ESP32.

    uint16_t pageaddr = 0;
    uint16_t pageaddrPrev = 0;
    uint8_t lineBuffer[256];
    uint8_t chsum = 0;
    uint8_t rtype = 0;
    uint8_t readedBins = 0;
    totalBins = 0;
    bool firstDataLine = true;
    uint32_t upperAddr = 0;       // старшие 16 бит адреса из записей типа 04
    uint32_t maxRealAddr = 0;     // максимальный реальный адрес (для проверки переполнения)

    // Читаем файл построчно
    while (file.available()) {
        String lineStr = file.readStringUntil('\n');
        lineStr.trim(); // убираем \r и пробелы

        // Пропускаем пустые строки
        if (lineStr.length() == 0) {
            continue;
        }

        // Должна начинаться с ':'
        if (lineStr[0] != ':') {
            continue; // пропускаем мусорные строки (некоторые HEX-файлы содержат комментарии)
        }

        // Парсим строку
        if (!hexFileLineParser(lineStr, pageaddr, lineBuffer, chsum, rtype, readedBins)) {
            return -9; // ERR_INCORRECTFILE
        }

        // Если тип 0x01 — конец файла
        if (rtype == 0x01) {
            break;
        }

        // Обработка Extended Linear Address (тип 04)
        if (rtype == 0x04) {
            if (readedBins >= 2) {
                upperAddr = ((uint32_t)lineBuffer[0] << 8) | (uint32_t)lineBuffer[1];
            }
            continue; // не сохраняем в binDataBuf
        }

        // Проверка монотонности адресов (только для data-строк)
        if (rtype == 0x00) {
            if (!firstDataLine) {
                if (pageaddrPrev > pageaddr) {
                    return -13; // ERR_HEXADDR
                }
            }
            pageaddrPrev = pageaddr;
            firstDataLine = false;
        }

        // Проверка контрольной суммы
        if (chsum != 0) {
            return -11; // ERR_HEXCRC
        }

        // Вычисляем реальный адрес данных в этой строке
        uint32_t realAddr = (upperAddr << 16) | pageaddr;

        // Проверка переполнения памяти чипа по реальному адресу
        uint32_t addrEnd = realAddr + readedBins;
        if (addrEnd > flashStartAddr + chipMemSize) {
            return -12; // ERR_HEXMEMOVER
        }

        // Отслеживаем максимальный реальный адрес для итоговой проверки
        if (addrEnd > maxRealAddr) {
            maxRealAddr = addrEnd;
        }

        // Копируем бинарные данные в выходной буфер
        if (readedBins > 0) {
            for (uint8_t i = 0; i < readedBins; i++) {
                binDataBuf.push_back((char)lineBuffer[i]);
            }
            totalBins += readedBins;
        }
    }

    if (totalBins == 0) {
        return -9; // ERR_INCORRECTFILE — нет данных
    }

    return (int32_t)totalBins;
}

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
                                hex_write_callback_t writeCallback, void *userData) {
    if (!file) {
        return -10; // ERR_NOFILE
    }
    if (!writeCallback) {
        return -9; // ERR_INCORRECTFILE — нет callback
    }

    // Защита pageSize: если 0 → 256, если > 1024 → 1024
    if (pageSize == 0) pageSize = 256;
    if (pageSize > 1024) pageSize = 1024;

    uint16_t pageaddr = 0;
    uint16_t pageaddrPrev = 0;
    uint8_t lineBuffer[256];
    uint8_t chsum = 0;
    uint8_t rtype = 0;
    uint8_t readedBins = 0;
    uint32_t totalBins = 0;
    bool firstDataLine = true;
    uint32_t upperAddr = 0;       // старшие 16 бит адреса из записей типа 04
    uint32_t maxRealAddr = 0;     // максимальный реальный адрес (для проверки переполнения)

    // Буфер для накопления непрерывного чанка данных
    // Размер равен pageSize (с защитой: 256..1024)
    std::vector<uint8_t> chunkBuf(pageSize);
    uint32_t chunkAddr = 0;
    uint32_t chunkPos = 0;
    bool chunkActive = false;

    // Вспомогательная функция для сброса накопленного чанка через callback
    // Используем лямбду, но для совместимости с C++ на ESP32 сделаем обычный блок
    // (лямбды в C++11 на ESP32 работают)

    // Читаем файл построчно
    while (file.available()) {
        String lineStr = file.readStringUntil('\n');
        lineStr.trim(); // убираем \r и пробелы

        // Пропускаем пустые строки
        if (lineStr.length() == 0) {
            continue;
        }

        // Должна начинаться с ':'
        if (lineStr[0] != ':') {
            continue; // пропускаем мусорные строки
        }

        // Парсим строку
        if (!hexFileLineParser(lineStr, pageaddr, lineBuffer, chsum, rtype, readedBins)) {
            return -9; // ERR_INCORRECTFILE
        }

        // Если тип 0x01 — конец файла
        if (rtype == 0x01) {
            break;
        }

        // Обработка Extended Linear Address (тип 04)
        if (rtype == 0x04) {
            if (readedBins >= 2) {
                upperAddr = ((uint32_t)lineBuffer[0] << 8) | (uint32_t)lineBuffer[1];
            }
            continue; // не пишем в flash
        }

        // Проверка монотонности адресов (только для data-строк)
        if (rtype == 0x00) {
            if (!firstDataLine) {
                if (pageaddrPrev > pageaddr) {
                    return -13; // ERR_HEXADDR
                }
            }
            pageaddrPrev = pageaddr;
            firstDataLine = false;
        }

        // Проверка контрольной суммы
        if (chsum != 0) {
            return -11; // ERR_HEXCRC
        }

        // Вычисляем реальный адрес данных в этой строке
        uint32_t realAddr = (upperAddr << 16) | pageaddr;

        // Проверка переполнения памяти чипа по реальному адресу
        uint32_t addrEnd = realAddr + readedBins;
        if (addrEnd > flashStartAddr + chipMemSize) {
            return -12; // ERR_HEXMEMOVER
        }

        // Отслеживаем максимальный реальный адрес для итоговой проверки
        if (addrEnd > maxRealAddr) {
            maxRealAddr = addrEnd;
        }

        // Если это первая data-строка или адрес не совпадает с ожидаемым —
        // сбрасываем накопленный чанк через callback
        if (readedBins > 0) {
            uint32_t expectedAddr = flashStartAddr + totalBins;
            if (realAddr != expectedAddr) {
                // Адрес не совпадает — возможно, это другой регион или есть пропуск
                // Сбрасываем текущий чанк, если он активен
                if (chunkActive && chunkPos > 0) {
                    int cbRet = writeCallback(chunkAddr, chunkBuf.data(), chunkPos, userData);
                    if (cbRet != 0) {
                        return -14; // ERR_HEXWRITE — callback вернул ошибку
                    }
                    chunkPos = 0;
                    chunkActive = false;
                }
                // Начинаем новый чанк
                chunkAddr = realAddr;
                chunkActive = true;
            } else if (!chunkActive) {
                // Первый чанк
                chunkAddr = realAddr;
                chunkActive = true;
            }

            // Копируем данные в чанк-буфер
            for (uint8_t i = 0; i < readedBins; i++) {
                if (chunkPos >= chunkBuf.size()) {
                    // Буфер чанка переполнен — сбрасываем через callback
                    int cbRet = writeCallback(chunkAddr, chunkBuf.data(), chunkPos, userData);
                    if (cbRet != 0) {
                        return -14; // ERR_HEXWRITE
                    }
                    chunkAddr += chunkPos;
                    chunkPos = 0;
                }
                chunkBuf.data()[chunkPos++] = lineBuffer[i];
            }
            totalBins += readedBins;
        }
    }

    // Сбрасываем последний накопленный чанк
    if (chunkActive && chunkPos > 0) {
        int cbRet = writeCallback(chunkAddr, chunkBuf.data(), chunkPos, userData);
        if (cbRet != 0) {
            return -14; // ERR_HEXWRITE
        }
    }

    if (totalBins == 0) {
        return -9; // ERR_INCORRECTFILE — нет данных
    }

    return (int32_t)totalBins;
}

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
int32_t hexFileGetBinarySize(File &file) {
    if (!file) {
        return -1;
    }

    uint32_t totalBins = 0;
    uint8_t lineBuffer[256];
    uint16_t pageaddr = 0;
    uint8_t chsum = 0;
    uint8_t rtype = 0;
    uint8_t readedBins = 0;

    // Читаем файл построчно
    while (file.available()) {
        String lineStr = file.readStringUntil('\n');
        lineStr.trim(); // убираем \r и пробелы

        // Пропускаем пустые строки
        if (lineStr.length() == 0) {
            continue;
        }

        // Должна начинаться с ':'
        if (lineStr[0] != ':') {
            continue; // пропускаем мусорные строки
        }

        // Парсим строку
        if (!hexFileLineParser(lineStr, pageaddr, lineBuffer, chsum, rtype, readedBins)) {
            return -1; // ошибка парсинга
        }

        // Если тип 0x01 — конец файла
        if (rtype == 0x01) {
            break;
        }

        // Учитываем только data-записи (тип 00)
        if (rtype == 0x00) {
            totalBins += readedBins;
        }
        // Записи типа 04 (Extended Linear Address) пропускаем — они не содержат данных прошивки
    }

    return (int32_t)totalBins;
}

/**
 * @brief Проверить, является ли файл HEX-форматом (по расширению).
 */
bool hexFileIsFormat(const String &path) {
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(".hex");
}
