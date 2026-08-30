#ifndef _MODULE_JSON_h
#define _MODULE_JSON_h
#include "main.h"

#ifdef DEBUG_JSON
#define DEBUGJSON(...) DBG_MOD("[C_JSON] ", __VA_ARGS__)
#else
#define DEBUGJSON(...)
#endif

#include <ArduinoJson.h>

#include "FSWebServerLib.h"
#include "mod_context.h"


#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif


class  CLASS_CORE_JSON    {
    
public:    
    CLASS_CORE_JSON (bool _in);
    void web_Init(void);
    void begin(ModContext& ctx);
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
protected: 
    #if ESP32
    fs::LittleFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    
public:
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif

    // ---- New public API (hides ArduinoJson) ----
    
    // Read a single key from a JSON file
    bool jsonFileReadStr(const String& file, const String& key, String& out);
    bool jsonFileReadInt(const String& file, const String& key, int32_t& out);
    bool jsonFileReadUint(const String& file, const String& key, uint32_t& out);
    bool jsonFileReadBool(const String& file, const String& key, bool& out);
    
    // Write a single key to a JSON file
    bool jsonFileWriteStr(const String& file, const String& key, const String& val);
    bool jsonFileWriteInt(const String& file, const String& key, int32_t val);
    bool jsonFileWriteBool(const String& file, const String& key, bool val);
    
    // Parse a JSON string (including nested paths like "filesystem.version.full_string")
    bool jsonParseStr(const String& json, const String& key, String& out);
    bool jsonParseInt(const String& json, const String& key, int32_t& out);
    bool jsonParseBool(const String& json, const String& key, bool& out);
    // Parse from nested path: jsonParseNestedStr(json, "filesystem|version|full_string", out)
    bool jsonParseNestedStr(const String& json, const String& path, String& out);
    bool jsonParseNestedInt(const String& json, const String& path, int32_t& out);
    bool jsonParseNestedInt64(const String& json, const String& path, int64_t& out);
    bool jsonParseNestedBool(const String& json, const String& path, bool& out);
    // Get array size at path (e.g. "files")
    int jsonGetArraySize(const String& json, const String& path);
    // Read string from array element at index: path[i].key
    bool jsonGetArrayStr(const String& json, const String& arrayPath, int index, const String& key, String& out);
    bool jsonGetArrayInt(const String& json, const String& arrayPath, int index, const String& key, int32_t& out);
    
    // Build a simple JSON string (single key-value)
    String jsonBuildObj(const String& key, const String& val);
    String jsonBuildObjInt(const String& key, int32_t val);

    // Multi-key JSON string building (for slot config with ip arrays)
    // Build slot JSON: {"ssid":"...","password":"...","dhcp":true/false,"ip":[...],...}
    String jsonBuildSlotConfig(const String& ssid, const String& password, bool dhcp,
                               const IPAddress& ip, const IPAddress& netmask,
                               const IPAddress& gateway, const IPAddress& dns);
    // Parse slot JSON from string body into fields
    // Returns number of fields parsed, or 0 on error
    int jsonParseSlotConfig(const String& json, String& ssid, String& password, bool& dhcp,
                            IPAddress& ip, IPAddress& netmask,
                            IPAddress& gateway, IPAddress& dns);

    // Read/write slot config directly from file
    bool jsonFileLoadSlot(const String& file, String& ssid, String& password, bool& dhcp,
                          IPAddress& ip, IPAddress& netmask,
                          IPAddress& gateway, IPAddress& dns);
    bool jsonFileSaveSlot(const String& file, const String& ssid, const String& password, bool dhcp,
                          const IPAddress& ip, const IPAddress& netmask,
                          const IPAddress& gateway, const IPAddress& dns);

public:
    // Internal: used by module_prog which includes ArduinoJson directly
    bool save_jsonDoc(const JsonDocument& jsonDoc, const String& file);
    bool load_jsonDoc(const String& file, JsonDocument& jsonDoc);
    bool jsonFileLoadDoc(const String& file, JsonDocument& doc);
    bool jsonFileSaveDoc(const String& file, JsonDocument& doc);
    
protected:
    bool  dumb = false;

};

extern CLASS_CORE_JSON core_json;


#endif // _MODULE_JSON_h
