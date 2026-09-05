#ifndef _DEVICE_E7RGB_h
#define _DEVICE_E7RGB_h

#include "main.h"

#include "mod_context.h"

#include "e7rgb_matrix.h"
#include "e7rgb_fonts.h"

#ifdef DEBUG_E7RGB
#define DEBUGE7RGB(...) DBG_MOD("[D_E7RGB] ", __VA_ARGS__)
#else
#define DEBUGE7RGB(...)
#endif

#include <LittleFS.h>

#define CONFIG_FILE_E7RGB   "/config_e7rgb.json"
#define E7_FONT_DIR         "/e7fonts"
#define E7_FONT_DIR_SLASH   "/e7fonts/"
#define E7_FONT_DEFAULT     "/e7fonts/digital7.fnt"

#define E7_MODE_WORK        0   // рабочий режим: часы ЧЧ:ММ (NTP)
#define E7_MODE_MANUAL      1   // ручной режим: свой символ в каждой из 4 цифр

typedef struct {
    uint8_t  mode;          // E7_MODE_WORK / E7_MODE_MANUAL
    int16_t  dataPin;       // пин данных (по умолчанию 16, -1 = выкл.)
    uint8_t  brightness;    // 0..255 (по умолчанию 25; масштабирование каналов)
    uint32_t digitsColor;   // 0xRRGGBB цвет цифр
    uint8_t  origin;        // E7_ORIGIN_* (дефолт НЛ — спаянная матрица)
    uint8_t  direction;     // E7_DIR_* (дефолт вверх)
    uint8_t  layout;        // E7_LAYOUT_* (дефолт зигзаг)
    String   fontFile;      // путь шрифта в ФС
    String   manualText;    // 4 символа для ручного режима ('0'..'9','-',' ')
} strE7RgbConfig;

class CLASS_DEVICE_E7RGB {
public:
    CLASS_DEVICE_E7RGB(bool _in);
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleInfo(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleFonts(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

    // Логика устройства
    bool loadFont();                       // загрузка шрифта из ФС (с автосозданием)
    void initMatrix();                     // создать ленту по конфигу
    void destroyMatrix();
    void applyMode();                      // применить текущий режим на матрицу
    void redrawFrame();                    // перерисовать рабочий кадр (время/прочерки)
    void renderDigits(uint8_t h, uint8_t m);
    void renderNoTime();
    void renderManual();                   // ручной режим: показать manualText
    void drawGlyphAt(uint8_t x0, const uint8_t* mask);  // нарисовать глиф в ячейке

    static void deferredApplyTask();

    friend void e7rgbSecondTask();

protected:
    bool dumb;
    fs::LittleFSFS* _fs;
    strE7RgbConfig _config;
    E7Matrix _matrix;
    E7Fonts _fonts;

    uint8_t _lastMinute;      // последняя нарисованная минута
    bool    _ntpWasSynced;    // первая синхронизация NTP (переход на часы)
    bool    _forceRedraw;     // принудительная перерисовка (настройки/режим)
    bool    _pendingReinit;   // отложенное пересоздание ленты (смена пина)
    bool    _pendingSave;     // отложенное сохранение конфига
    bool    _pendingApply;    // отложенное применение
    int16_t _pendingDataPin;  // новый пин (для _pendingReinit)
};

extern CLASS_DEVICE_E7RGB device_electronica7_rgb;

#endif // _DEVICE_E7RGB_h
