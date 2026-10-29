#ifndef _MODULENTP_h
#define _MODULENTP_h

#include "main.h"
#include <WiFiClient.h>
#include "common/TimeLib.h"

#include "mod_context.h"


#include "core_ntp/NtpClientLib.h"
#include "core_ntp_types.h"



#ifdef DEBUG_NTP
#define DEBUGNTP(...) DBG_MOD("[C_NTP] ", __VA_ARGS__)
#else
#define DEBUGNTP(...)
#endif


const char Page_GeneralNtp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/ntp.html">
Please Wait....Configuring.
)=====";

class  CLASS_CORE_NTP    {
    public:

    #if ESP32
    fs::LittleFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    
    
    strNtpConfig    _ntpConfig; // NTP configuration
    
    void begin ();
    void begin(ModContext& ctx);
    void ntpOnConnected ();
    void ntpOnDisconected ();
    void web_Init();
    // Отложенные действия (применение конфига, failover) — выполняются в главном цикле.
    void loop();
    void registerTimeSource();   // Time Source Provider API (core_sys)
    void applyTimeZone();        // переустановить TZ/DST в NTPClientLib

    // Состояние последнего синка (для /ntp/info и диагностики).
    uint32_t    ntpLastSyncEpoch() const;
    const char* ntpStatusText() const;
    bool        ntpTimeFresh() const;
    
    void ntpSwitchReserv ();
    void ntpOnSyncHandler(NTPSyncEvent_t event);
    
    //CFG
    bool load_config_NTP();
    bool save_configNTP();
    void defaultConfigNTP();
    // WEB
    
    void send_NTP_info_html(AsyncWebServerRequest *request) ;
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void html2ntp_configuration(AsyncWebServerRequest *request);
    
private:
    // Адаптивный stale по текущему периоду (реализация — core_ntp_engine.cpp).
    void ntpUpdateStaleSec();
    // Проверка конфига перед применением: nullptr при успехе, иначе текст ошибки.
    const char* ntpValidateConfig(const String& s0, const String& s1, const String& s2, long period);
    // Единая точка применения конфига в рантайме (после присвоения _ntpConfig).
    void applyNtpRuntime();
    // Применить сервер по индексу (только из loop()).
    void ntpSetServer(int idx);

    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
    
    protected: 
    bool updateTimeFromNTP  = false;
    int _ntpServerCount         = 0;
    String _ntpServerNow = "";
    // Конфиг применён не полностью: применить в loop() (веб-задача не трогает NTPClient).
    volatile bool _pendingApply = false;
    // Остановить провайдер в loop() (событие Wi-Fi приходит из системной задачи).
    volatile bool _pendingStop = false;
    // Провайдер TimeLib запущен (NTP.begin() выполнен).
    bool _ntpStarted = false;
};

extern CLASS_CORE_NTP core_ntp;





#endif // _MODULENTP_h