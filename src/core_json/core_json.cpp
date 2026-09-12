

#include "main.h"
#include <ArduinoJson.h>


#include "core_web/FSWebServerLib.h"
#include "debug.h"
#include "core_json.h"
#include "core_json_version.h"
CLASS_CORE_JSON core_json;

#if defined(ESP32)
    void CLASS_CORE_JSON::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
    void CLASS_CORE_JSON::setFs(FS* fs)	// esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

void CLASS_CORE_JSON::begin(ModContext& ctx) {
    _fs = ctx.fs;
}

void CLASS_CORE_JSON::web_Init(void) {
    ESPHTTPServer.on("/json/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_CORE_JSON::getVersionStr(){
    return String(CORE_JSON_VERSION);
}

String CLASS_CORE_JSON::getGeneratedTime(){
    return String(CORE_JSON_GENERATED_TIME);
}

String CLASS_CORE_JSON::getCommitDateStr(){
    return String(CORE_JSON_COMMIT_DATE_STR);
}

void CLASS_CORE_JSON::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGJSON("%s\n\r", __FUNCTION__);
    String values = "";
    values += "jsnversion|"     + getVersionStr()    + "|div\n";
    values += "jsngentime|"     + getGeneratedTime() + "|div\n";
    values += "jsngendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
