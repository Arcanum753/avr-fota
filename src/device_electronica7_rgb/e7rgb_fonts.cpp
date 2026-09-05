#include "e7rgb_fonts.h"

#include <pgmspace.h>

// ============================================================
// e7rgb_fonts.cpp — реализация менеджера шрифтов.
// Формат .fnt (текстовый):
//   '#' и пустые строки игнорируются;
//   маркер глифа: # '0' ... # '9', # '-';
//   маска: 7 строк по 4 колонки вида {1,0,0,1}
//   (строки сверху вниз, колонки слева направо, 1 = горит).
// Парсер терпимый: принимает и старый формат («символ + строки 1111»).
// ============================================================

E7Fonts::E7Fonts() {
    loadDefault();
}

// Индекс глифа в _glyphs по символу ('0'..'9' -> 0..9, '-' -> 10)
int E7Fonts::slotOf(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c == '-')             { return E7_FONTS_DIGIT_COUNT; }
    return -1;
}

// Заполнить маски встроенными таблицами по умолчанию (PROGMEM)
void E7Fonts::loadDefault() {
    for (int d = 0; d < E7_FONTS_DIGIT_COUNT; d++) {
        for (int r = 0; r < E7_GLYPH_H; r++) {
            for (int c = 0; c < E7_GLYPH_W; c++) {
                _glyphs[d][r][c] = pgm_read_byte(&E7_FONT_DEFAULT_DIGITS[d][r][c]);
            }
        }
    }
    for (int r = 0; r < E7_GLYPH_H; r++) {
        for (int c = 0; c < E7_GLYPH_W; c++) {
            _glyphs[E7_FONTS_DIGIT_COUNT][r][c] = pgm_read_byte(&E7_FONT_DEFAULT_DASH[r][c]);
        }
    }
}

// Маска глифа как плоский массив [ряд*E7_GLYPH_W + колонка], значения 0/1
const uint8_t* E7Fonts::glyph(char c) const {
    int slot = slotOf(c);
    if (slot < 0) { return NULL; }
    return &_glyphs[slot][0][0];
}

// Парсинг файла шрифта. Возвращает true только если разобраны все 10 цифр.
bool E7Fonts::loadFile(fs::FS& fs, const char* path) {
    File f = fs.open(path, FILE_READ);
    if (!f) { return false; }

    String text;
    while (f.available()) { text += (char)f.read(); }
    f.close();

    // Проверка формата: файлы без маркера текущего формата (более старые
    // версии шрифтов из ФС) считаем устаревшими — они пересоздаются из
    // встроенных таблиц при следующем loadOrCreate().
    if (text.indexOf("# FORMAT=3") < 0) {
        loadDefault();
        return false;
    }

    bool  digitsOk[E7_FONTS_DIGIT_COUNT] = {false, false, false, false, false,
                                             false, false, false, false, false};
    uint8_t collectedDigits = 0;

    uint8_t tmp[E7_GLYPH_H][E7_GLYPH_W] = {{0}};
    uint8_t rowsGot = 0;
    int     slot = 0;          // следующий незаполненный глиф

    String line;
    int len = text.length();
    for (int pos = 0; pos <= len; pos++) {
        char ch = (pos < len) ? text.charAt(pos) : '\n';
        if (ch != '\n') { line += ch; continue; }

        line.trim();
        if (line.length() == 0 || line.charAt(0) == '#') { line = ""; continue; }

        // Снимаем '{' '}' ',' пробелы и табуляцию
        String clean;
        for (unsigned int i = 0; i < line.length(); i++) {
            char cc = line.charAt(i);
            if (cc == '{' || cc == '}' || cc == ',' || cc == ' ' || cc == '\t') { continue; }
            clean += cc;
        }
        if (clean.length() == 0) { line = ""; continue; }

        // Одиночные символы-маркеры ('0'..'9', '-') игнорируем (легаси/совместимость)
        if (clean.length() == 1) {
            char mc = clean.charAt(0);
            if ((mc >= '0' && mc <= '9') || mc == '-') { line = ""; continue; }
        }

        // Строка маски: не менее E7_GLYPH_W символов только '0'/'1'
        if (clean.length() >= E7_GLYPH_W) {
            bool rowOk = true;
            for (int c = 0; c < E7_GLYPH_W; c++) {
                char v = clean.charAt(c);
                if (v != '0' && v != '1') { rowOk = false; break; }
            }
            if (rowOk) {
                for (int c = 0; c < E7_GLYPH_W; c++) { tmp[rowsGot][c] = (clean.charAt(c) == '1'); }
                rowsGot++;
                if (rowsGot == E7_GLYPH_H) {
                    if (slot < E7_FONTS_TOTAL_GLYPHS) {
                        for (int r = 0; r < E7_GLYPH_H; r++) {
                            for (int c = 0; c < E7_GLYPH_W; c++) { _glyphs[slot][r][c] = tmp[r][c]; }
                        }
                        if (slot < E7_FONTS_DIGIT_COUNT && !digitsOk[slot]) {
                            digitsOk[slot] = true;
                            collectedDigits++;
                        }
                        slot++;
                    }
                    rowsGot = 0;
                }
                line = "";
                continue;
            }
        }

        // Непохожая на маску строка — игнорируем
        line = "";
    }

    if (collectedDigits != E7_FONTS_DIGIT_COUNT) {
        loadDefault();
        return false;
    }
    return true;
}

// Файл в ФС в приоритете; если его нет (или он повреждён) —
// создаём из встроенных таблиц и читаем уже созданный файл.
bool E7Fonts::loadOrCreate(fs::FS& fs, const char* path) {
    if (loadFile(fs, path)) { return true; }
    loadDefault();
    if (save(fs, path)) { return loadFile(fs, path); }
    return false;
}

bool E7Fonts::save(fs::FS& fs, const char* path) const {
    File f = fs.open(path, FILE_WRITE);
    if (!f) { return false; }

    f.print("# E7FONT digital7\n");
    f.print("# FORMAT=3\n");
    f.print("# Глиф: маркер '# 'N'', затем 7 строк по 4 колонки\n");
    f.print("# {кол.0,кол.1,кол.2,кол.3} — строки сверху вниз, 1 = горит.\n");
    for (int i = 0; i < E7_FONTS_TOTAL_GLYPHS; i++) {
        if (i < E7_FONTS_DIGIT_COUNT) {
            f.print("# '");
            f.print((char)('0' + i));
            f.print("'\n");
        } else {
            f.print("# '-'\n");
        }
        for (int r = 0; r < E7_GLYPH_H; r++) {
            f.print("{");
            for (int c = 0; c < E7_GLYPH_W; c++) {
                if (c > 0) { f.print(","); }
                f.print(_glyphs[i][r][c] ? '1' : '0');
            }
            f.print("}\n");
        }
    }
    f.close();
    return true;
}

// Список файлов шрифтов в каталоге (имена без пути, построчно).
// Устройство ESP32-only: используем open()/openNextFile() (API ESP32).
void E7Fonts::listFonts(fs::FS& fs, const char* dir, String& out) const {
    out = "";
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) { return; }
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory() == false) {
            String name = String(entry.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) { name = name.substring(slash + 1); }
            if (name.endsWith(".fnt")) {
                out += name;
                out += '\n';
            }
        }
        entry = root.openNextFile();
    }
}
