#ifndef _CORE_SYS_H
#define _CORE_SYS_H

#include "main.h"

#include <Arduino.h>

#include "mod_context.h"

#include "core_web/FSWebServerLib.h"

#if defined(DEBUG_SYS)
#define DEBUGSYS(...) DBG_MOD("[C_SYS] ", __VA_ARGS__)
#else
#define DEBUGSYS(...)
#endif


#define CONFIG_FILE_SYS             "/config_sys.json"
#define SECRET_FILE                 "/secret.json"

#define FS_VERSION_JSON_PATH        "/_version_fs.json"


typedef struct {
    String deviceName;
    String deviceSerial;
} strSysConfig;


typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
    String wwwQuestion;
    String wwwAnswer;
} strHTTPAuth;


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

private:
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
};

extern CLASS_CORE_SYS core_sys;

#endif // _CORE_SYS_H
