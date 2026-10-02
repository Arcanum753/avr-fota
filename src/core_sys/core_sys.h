#ifndef _CORE_SYS_H
#define _CORE_SYS_H

#include "main.h"

#include <Arduino.h>

#include "mod_context.h"

#include "core_web/FSWebServerLib.h"
#include "core_state/core_state_types.h"

#if defined(DEBUG_SYS)
#define DEBUGSYS(...) DBG_MOD("[C_SYS] ", __VA_ARGS__)
#else
#define DEBUGSYS(...)
#endif


#include "core_sys_types.h"


const char Page_IndexRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_GeneralSys[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/system.html">
Please Wait....Configuring.
)=====";


// ============================================================
// Системное ядро: идентичность устройства, конфиг системы,
// HTTP-авторизация/восстановление и информация о системе.
// ============================================================
class CLASS_CORE_SYS {
public:
    void begin(ModContext& ctx);
    void register_resources();
    void web_Init();

    // --- Идентичность и системный конфиг ---
    const String getHostName();
    String getDeviceName();
    void setDeviceName(const String& name);
    String getDeviceSerial();
    void setDeviceSerial(const String& serial);
    void defaultConfigSys();
    bool save_configSys();
    bool saveSysIdentStore();

    // --- Информация о системе ---
    String getResetReason();
    void serialShowAbout();
    String getFsVersionStr();
    void invalidateFsVersionCache();
    bool getFsVersion(int64_t& date, int32_t& build, int32_t& major, int32_t& minor);

    // --- HTTP-авторизация ---
    bool checkAuth(AsyncWebServerRequest *request);
    bool httpAuthEnabled();
    String getHttpPassword();

    // --- Виртуальное время системы ---
    // Регистрация источника времени (Time Source Provider API).
    bool    addTimeSource(const char* name, int prio,
                          TimeGetFn get, TimeSetFn set = nullptr,
                          TimeStatusFn status = nullptr);
    time_t  timeNow() const;
    bool    timeValid() const;
    const char* timeSourceName() const;
    int8_t  timeZoneHours() const;
    int8_t  timeZoneMinutes() const;
    bool    daylight() const;
    void    startTimeService();   // планирует core_task.every("time.tick", 1000)
    void    registerTimeResources();   // регистрация time.* (реализация — core_sys_time.cpp)

protected:
    // --- Виртуальное время системы (реализация — core_sys_time.cpp; protected
    //     для host-тестов через TestSys-наследник) ---
    void   timeTick();
    void   timeEmitSources();
    void   timeUpdateDerived();
    void   applyTimeSet(time_t v);
    bool   load_config_Time();
    bool   save_config_Time();
    int    findTimeSource(const char* name) const;
    void   handleTimeInfo(AsyncWebServerRequest *request);
    void   handleTimeSources(AsyncWebServerRequest *request);
    void   handleTimeSave(AsyncWebServerRequest *request);
    void   handleTimeSet(AsyncWebServerRequest *request);
    void   handleTimeSync(AsyncWebServerRequest *request);
    void   handleTimeVer(AsyncWebServerRequest *request);

private:
    static int cmdTimeSet     (void* user, int argc, const BusValue* argv, BusValue& result);
    static int cmdTimeSyncFrom(void* user, int argc, const BusValue* argv, BusValue& result);
    static int cmdTimeSave    (void* user, int argc, const BusValue* argv, BusValue& result);
    static void cbTimeTick();
    static void cbTimeSaveTask();

    // Конфиг системы и identity
    bool load_config_Sys();
    void loadDeviceIdent(bool fsOk);

    // HTTP-авторизация
    bool loadHTTPAuth();
    bool saveHTTPAuth();

    // Веб-обработчики
    void html_send_chipinfo(AsyncWebServerRequest *request);
    void html_system_Load(AsyncWebServerRequest *request);
    void html_system_Save(AsyncWebServerRequest *request);
    void html_version_info(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void set_wwwauth_configuration(AsyncWebServerRequest *request);
    void recover_status_values_html(AsyncWebServerRequest *request);
    void recover_reset(AsyncWebServerRequest *request);

    // FS-версия
    void cacheFsVersionInfo();
    bool parseVersionFromJson(const String& jsonStr, int64_t& date, int32_t& build, int32_t& major, int32_t& minor);

protected:
#if defined(ESP32)
    fs::LittleFSFS*               _fs = nullptr;
#endif
#if defined(ESP8266)
    FS*                         _fs = nullptr;                  // esp8266/esp32 flash file system
#endif
    strSysConfig                _sysConfig;
    strHTTPAuth                 _httpAuth;

    bool     _fsVersionCached = false;
    bool     _fsVersionValid = false;
    int64_t  _cachedFsDate = 0;
    int32_t  _cachedFsBuild = 0;
    int32_t  _cachedFsMajor = 0;
    int32_t  _cachedFsMinor = 0;
    String   _cachedFsVersionStr = "";

    // Подсистема виртуального времени
    strTimeSource _timeSrc[CORE_SYS_TIME_MAX_SOURCES];
    uint8_t       _timeSrcCount      = 0;
    uint8_t       _timeActiveIdx     = 255;   // 255 = активного нет
    time_t        _timeLastValid     = 0;     // 0 = валидного ещё не было
    uint32_t      _timeLastValidMs   = 0;     // millis() последнего валидного такта
    int32_t       _timeTzDec         = 0;     // часовой пояс, десятые доли часа
    bool          _timeDst           = false;
    uint32_t      _timeSyncIntervalS = 3600;  // обратная синхронизация RTC, сек (0 = off)
    uint32_t      _timeLastRtcSync   = 0;     // millis()/1000 последней реверс-синхронизации
    bool          _timePendingSave   = false;
    bool          _timeTickArmed     = false;
};

extern CLASS_CORE_SYS core_sys;

#endif // _CORE_SYS_H
