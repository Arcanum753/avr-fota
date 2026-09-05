#ifndef _E7RGB_FONTS_h
#define _E7RGB_FONTS_h

// ============================================================
// e7rgb_fonts.h — менеджер шрифтов (файл общего назначения).
// Шрифт — битовая маска 0/1 7x4 без цвета. Источник:
//   1) файл в ФС (/e7fonts/*.fnt) — приоритет;
//   2) встроенные таблицы по умолчанию из e7rgb_font.h.
// Если файла в ФС нет — создаём его из встроенных таблиц.
//
// Формат .fnt (текстовый, читаемый «глазами»):
//   - строки с '#' и пустые строки игнорируются;
//   - маркер глифа: # '0', # '1', ..., # '9', # '-';
//   - маска: 7 строк вида {1,0,0,1} (колонки слева направо,
//     строки сверху вниз, 1 = горит);
//   - глифы идут блоками по 7 строк в порядке '0'..'9', '-'.
// ============================================================

#include <Arduino.h>
#include <LittleFS.h>

#include "e7rgb_font.h"

#define E7_FONTS_DIGIT_COUNT     10    // глифы '0'..'9'
#define E7_FONTS_TOTAL_GLYPHS    (E7_FONTS_DIGIT_COUNT + 1)   // + прочерк '-'

class E7Fonts {
public:
    E7Fonts();
    void loadDefault();                                   // заполнить из PROGMEM-таблиц
    const uint8_t* glyph(char c) const;                   // маска [E7_GLYPH_H*E7_GLYPH_W] или NULL
    bool loadOrCreate(fs::FS& fs, const char* path);      // файл в приоритете, иначе создать дефолт
    bool save(fs::FS& fs, const char* path) const;        // записать текущие маски в .fnt
    void listFonts(fs::FS& fs, const char* dir, String& out) const; // имена *.fnt построчно

private:
    bool loadFile(fs::FS& fs, const char* path);          // парсинг .fnt (без автосоздания)
    static int  slotOf(char c);                           // индекс глифа или -1

    // Хранение: [глиф][ряд][колонка], значения 0/1
    uint8_t _glyphs[E7_FONTS_TOTAL_GLYPHS][E7_GLYPH_H][E7_GLYPH_W];
};

#endif // _E7RGB_FONTS_h
