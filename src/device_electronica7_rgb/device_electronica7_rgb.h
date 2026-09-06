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

// Эффекты окраски отображаемого
#define E7_EFFECT_MONO          0   // моноцвет (digitsColor)
#define E7_EFFECT_RAINBOW       1   // радуга с переливом
#define E7_EFFECT_GRAD_STATIC   2   // градиент из 2 цветов, статичный
#define E7_EFFECT_GRAD_DYNAMIC  3   // градиент из 2 цветов, динамический
#define E7_EFFECT_COLORCYCLE    4   // плавная смена цвета по палитре

// Режимы обхода палитры для E7_EFFECT_COLORCYCLE
#define E7_CYCLE_SEQUENTIAL 0      // по порядку, заданному пользователем
#define E7_CYCLE_RANDOM     1      // произвольно (случайный порядок на круг)
#define E7_CYCLE_RAINBOW    2      // по радуге (палитра не используется)

#define E7_FX_MAX_COLORS    8      // максимум цветов в палитре

// Направления перелива/градиента
#define E7_FXDIR_TL   0   // из верхнего левого угла (по диагонали)
#define E7_FXDIR_TR   1   // из верхнего правого угла
#define E7_FXDIR_BL   2   // из нижнего левого угла
#define E7_FXDIR_BR   3   // из нижнего правого угла
#define E7_FXDIR_TB   4   // сверху вниз
#define E7_FXDIR_BT   5   // снизу вверх

#define E7RGB_ANIM_MS  100   // период анимации динамических эффектов

typedef struct {
    uint8_t  mode;          // E7_MODE_WORK / E7_MODE_MANUAL
    int16_t  dataPin;       // пин данных (по умолчанию 16, -1 = выкл.)
    uint8_t  brightness;    // 0..255 (по умолчанию 25; масштабирование каналов)
    uint8_t  effect;        // E7_EFFECT_*
    uint8_t  effectDir;     // E7_FXDIR_* — направление перелива/градиента
    uint32_t digitsColor;   // 0xRRGGBB — цвет 1 (моноцвет / начало градиента)
    uint32_t digitsColor2;  // 0xRRGGBB — цвет 2 (конец градиента)
    uint8_t  animSpeed;     // 1..50 — скорость перелива (градус фазы за тик)
    uint8_t  colorsCount;   // 1..8 — кол-во цветов палитры (смена цвета)
    uint8_t  cycleMode;     // E7_CYCLE_* — режим обхода палитры
    uint32_t palette[E7_FX_MAX_COLORS];   // палитра для плавной смены цвета
    uint8_t  origin;        // E7_ORIGIN_* (дефолт НЛ — спаянная матрица)
    uint8_t  direction;     // E7_DIR_* (дефолт вверх)
    uint8_t  layout;        // E7_LAYOUT_* (дефолт параллельно)
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
    friend void e7rgbAnimTask();

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
    float   _animPhase;       // фаза анимации эффекта (0..360 градусов)
    uint32_t _fxLap;          // номер «круга» обхода палитры (для случайного порядка)
    uint8_t  _fxOrder[E7_FX_MAX_COLORS];  // текущий порядок обхода палитры
};

extern CLASS_DEVICE_E7RGB device_electronica7_rgb;

#endif // _DEVICE_E7RGB_h
