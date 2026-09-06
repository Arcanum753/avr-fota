#include "format_bin.h"

/**
 * @brief Открыть BIN-файл из файловой системы.
 *
 * Автоматически добавляет ведущий '/' если его нет.
 * Возвращает File-объект. Проверка успеха: if (!file) возвращает "пустой" File.
 */
File binFileOpen(fs::FS &fs, const String &path) {
    String fullPath = path;
    if (!fullPath.startsWith("/")) {
        fullPath = "/" + fullPath;
    }
    return fs.open(fullPath, "rb");
}

/**
 * @brief Получить размер открытого BIN-файла.
 *
 * Перемещает указатель в конец, читает позицию, возвращает в начало.
 */
uint32_t binFileGetSize(File &file) {
    if (!file) return 0;
    file.seek(0, SeekEnd);
    uint32_t size = file.position();
    file.seek(0, SeekSet);
    return size;
}

/**
 * @brief Прочитать одну страницу (порцию) данных из BIN-файла.
 *
 * Читает не более bufferSize байт в buffer.
 * Возвращает количество реально прочитанных байт.
 */
uint32_t binFileReadPage(File &file, uint8_t *buffer, uint32_t bufferSize) {
    if (!file || !buffer || bufferSize == 0) return 0;
    return file.read(buffer, bufferSize);
}

/**
 * @brief Закрыть BIN-файл.
 */
void binFileClose(File &file) {
    if (file) {
        file.close();
    }
}

/**
 * @brief Проверить, является ли файл BIN-форматом (по расширению).
 *
 * Проверяет окончание строки на ".bin" или ".binary" (регистронезависимо).
 */
bool binFileIsFormat(const String &path) {
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(".bin") || lower.endsWith(".binary");
}
