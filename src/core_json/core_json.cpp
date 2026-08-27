

#include "main.h"
#include <ArduinoJson.h>


#include "FSWebServerLib.h"
#include "debug.h"
#include "core_json.h"
#include "core_json_version.h"
CLASS_CORE_JSON ModClassJson(false);

CLASS_CORE_JSON :: CLASS_CORE_JSON (bool _in) {
	dumb = _in;
}

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

bool CLASS_CORE_JSON::load_jsonDoc(const String& file, JsonDocument& jsonDoc){
	if (!_fs) return false;
	File configFile = _fs->open(file, "r");

	if (configFile == false) {
		DEBUGJSON("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUGJSON("Config file size is too large");
	configFile.close();
	return false;
	}*/
	char * buf = (char *) malloc(size);
	if ( buf == NULL){ return false;	}
	DEBUGJSON("File: %s, size: %d\r\n", file.c_str(), size);
	configFile.readBytes(buf, size);
	configFile.close();
	auto error = deserializeJson(jsonDoc, buf);
	free(buf);
	if (error) {
		DEBUGJSON("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}
	return true;
}


bool CLASS_CORE_JSON::save_jsonDoc(const JsonDocument& jsonDoc,	const String& file) {
	if (!_fs) return false;
	File configFile  = _fs->open(file, "w");
	if (configFile == false) {
		DEBUGJSON("Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}
#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

// ========== New public API ==========

bool CLASS_CORE_JSON::jsonFileReadStr(const String& file, const String& key, String& out) {
    JsonDocument doc;
    if (!load_jsonDoc(file, doc)) return false;
    out = doc[key].as<const char *>();
    return true;
}

bool CLASS_CORE_JSON::jsonFileReadInt(const String& file, const String& key, int32_t& out) {
    JsonDocument doc;
    if (!load_jsonDoc(file, doc)) return false;
    out = doc[key].as<int32_t>();
    return true;
}

bool CLASS_CORE_JSON::jsonFileReadUint(const String& file, const String& key, uint32_t& out) {
    JsonDocument doc;
    if (!load_jsonDoc(file, doc)) return false;
    out = doc[key].as<uint32_t>();
    return true;
}

bool CLASS_CORE_JSON::jsonFileReadBool(const String& file, const String& key, bool& out) {
    JsonDocument doc;
    if (!load_jsonDoc(file, doc)) return false;
    out = doc[key].as<bool>();
    return true;
}

bool CLASS_CORE_JSON::jsonFileWriteStr(const String& file, const String& key, const String& val) {
    JsonDocument doc;
    load_jsonDoc(file, doc);
    doc[key] = val;
    return save_jsonDoc(doc, file);
}

bool CLASS_CORE_JSON::jsonFileWriteInt(const String& file, const String& key, int32_t val) {
    JsonDocument doc;
    load_jsonDoc(file, doc);
    doc[key] = val;
    return save_jsonDoc(doc, file);
}

bool CLASS_CORE_JSON::jsonFileWriteBool(const String& file, const String& key, bool val) {
    JsonDocument doc;
    load_jsonDoc(file, doc);
    doc[key] = val;
    return save_jsonDoc(doc, file);
}

bool CLASS_CORE_JSON::jsonParseStr(const String& json, const String& key, String& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    out = doc[key].as<const char *>();
    return true;
}

bool CLASS_CORE_JSON::jsonParseInt(const String& json, const String& key, int32_t& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    out = doc[key].as<int32_t>();
    return true;
}

bool CLASS_CORE_JSON::jsonParseBool(const String& json, const String& key, bool& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    out = doc[key].as<bool>();
    return true;
}

String CLASS_CORE_JSON::jsonBuildObj(const String& key, const String& val) {
    JsonDocument doc;
    doc[key] = val;
    String out;
    serializeJson(doc, out);
    return out;
}

String CLASS_CORE_JSON::jsonBuildObjInt(const String& key, int32_t val) {
    JsonDocument doc;
    doc[key] = val;
    String out;
    serializeJson(doc, out);
    return out;
}

bool CLASS_CORE_JSON::jsonParseNestedStr(const String& json, const String& path, String& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = path.indexOf('|', start);
        String segment = (delim < 0) ? path.substring(start) : path.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    out = current.as<const char *>();
    return true;
}

bool CLASS_CORE_JSON::jsonParseNestedInt(const String& json, const String& path, int32_t& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = path.indexOf('|', start);
        String segment = (delim < 0) ? path.substring(start) : path.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    out = current.as<int32_t>();
    return true;
}

bool CLASS_CORE_JSON::jsonParseNestedInt64(const String& json, const String& path, int64_t& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = path.indexOf('|', start);
        String segment = (delim < 0) ? path.substring(start) : path.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    out = current.as<int64_t>();
    return true;
}

bool CLASS_CORE_JSON::jsonParseNestedBool(const String& json, const String& path, bool& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = path.indexOf('|', start);
        String segment = (delim < 0) ? path.substring(start) : path.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    out = current.as<bool>();
    return true;
}

int CLASS_CORE_JSON::jsonGetArraySize(const String& json, const String& path) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return 0;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = path.indexOf('|', start);
        String segment = (delim < 0) ? path.substring(start) : path.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return 0;
        if (delim < 0) break;
        start = delim + 1;
    }
    return current.as<JsonArray>().size();
}

bool CLASS_CORE_JSON::jsonGetArrayStr(const String& json, const String& arrayPath, int index, const String& key, String& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = arrayPath.indexOf('|', start);
        String segment = (delim < 0) ? arrayPath.substring(start) : arrayPath.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    JsonArray arr = current.as<JsonArray>();
    if (index < 0 || index >= (int)arr.size()) return false;
    out = arr[index][key].as<const char *>();
    return true;
}

bool CLASS_CORE_JSON::jsonGetArrayInt(const String& json, const String& arrayPath, int index, const String& key, int32_t& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    JsonVariant current = doc.as<JsonVariant>();
    int start = 0;
    while (true) {
        int delim = arrayPath.indexOf('|', start);
        String segment = (delim < 0) ? arrayPath.substring(start) : arrayPath.substring(start, delim);
        if (segment.length() == 0) break;
        current = current[segment];
        if (current.isNull()) return false;
        if (delim < 0) break;
        start = delim + 1;
    }
    JsonArray arr = current.as<JsonArray>();
    if (index < 0 || index >= (int)arr.size()) return false;
    out = arr[index][key].as<int32_t>();
    return true;
}

String CLASS_CORE_JSON::jsonBuildSlotConfig(const String& ssid, const String& password, bool dhcp,
                                             const IPAddress& ip, const IPAddress& netmask,
                                             const IPAddress& gateway, const IPAddress& dns) {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["dhcp"] = dhcp;

    JsonArray ipArr = doc["ip"].to<JsonArray>();
    ipArr.add(ip[0]); ipArr.add(ip[1]); ipArr.add(ip[2]); ipArr.add(ip[3]);

    JsonArray nmArr = doc["netmask"].to<JsonArray>();
    nmArr.add(netmask[0]); nmArr.add(netmask[1]); nmArr.add(netmask[2]); nmArr.add(netmask[3]);

    JsonArray gwArr = doc["gateway"].to<JsonArray>();
    gwArr.add(gateway[0]); gwArr.add(gateway[1]); gwArr.add(gateway[2]); gwArr.add(gateway[3]);

    JsonArray dnsArr = doc["dns"].to<JsonArray>();
    dnsArr.add(dns[0]); dnsArr.add(dns[1]); dnsArr.add(dns[2]); dnsArr.add(dns[3]);

    String out;
    serializeJson(doc, out);
    return out;
}

int CLASS_CORE_JSON::jsonParseSlotConfig(const String& json, String& ssid, String& password, bool& dhcp,
                                          IPAddress& ip, IPAddress& netmask,
                                          IPAddress& gateway, IPAddress& dns) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return 0;

    int count = 0;
    if (doc.containsKey("ssid")) { ssid = doc["ssid"].as<String>(); count++; }
    if (doc.containsKey("password")) { password = doc["password"].as<String>(); count++; }
    if (doc.containsKey("dhcp")) { dhcp = doc["dhcp"].as<bool>(); count++; }
    if (doc["ip"].is<JsonArray>()) {
        JsonArray arr = doc["ip"].as<JsonArray>();
        ip = IPAddress(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>(), arr[3].as<uint8_t>());
        count++;
    }
    if (doc["netmask"].is<JsonArray>()) {
        JsonArray arr = doc["netmask"].as<JsonArray>();
        netmask = IPAddress(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>(), arr[3].as<uint8_t>());
        count++;
    }
    if (doc["gateway"].is<JsonArray>()) {
        JsonArray arr = doc["gateway"].as<JsonArray>();
        gateway = IPAddress(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>(), arr[3].as<uint8_t>());
        count++;
    }
    if (doc["dns"].is<JsonArray>()) {
        JsonArray arr = doc["dns"].as<JsonArray>();
        dns = IPAddress(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>(), arr[3].as<uint8_t>());
        count++;
    }
    return count;
}

bool CLASS_CORE_JSON::jsonFileLoadSlot(const String& file, String& ssid, String& password, bool& dhcp,
                                        IPAddress& ip, IPAddress& netmask,
                                        IPAddress& gateway, IPAddress& dns) {
    JsonDocument doc;
    if (!load_jsonDoc(file, doc)) return false;
    ssid = doc["ssid"].as<const char *>();
    password = doc["pass"].as<const char *>();
    dhcp = doc["dhcp"].as<bool>();
    ip = IPAddress(doc["ip"][0], doc["ip"][1], doc["ip"][2], doc["ip"][3]);
    netmask = IPAddress(doc["netmask"][0], doc["netmask"][1], doc["netmask"][2], doc["netmask"][3]);
    gateway = IPAddress(doc["gateway"][0], doc["gateway"][1], doc["gateway"][2], doc["gateway"][3]);
    dns = IPAddress(doc["dns"][0], doc["dns"][1], doc["dns"][2], doc["dns"][3]);
    return true;
}

bool CLASS_CORE_JSON::jsonFileSaveSlot(const String& file, const String& ssid, const String& password, bool dhcp,
                                        const IPAddress& ip, const IPAddress& netmask,
                                        const IPAddress& gateway, const IPAddress& dns) {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["pass"] = password;
    doc["dhcp"] = dhcp;

    JsonArray ipArr = doc["ip"].to<JsonArray>();
    ipArr.add(ip[0]); ipArr.add(ip[1]); ipArr.add(ip[2]); ipArr.add(ip[3]);

    JsonArray nmArr = doc["netmask"].to<JsonArray>();
    nmArr.add(netmask[0]); nmArr.add(netmask[1]); nmArr.add(netmask[2]); nmArr.add(netmask[3]);

    JsonArray gwArr = doc["gateway"].to<JsonArray>();
    gwArr.add(gateway[0]); gwArr.add(gateway[1]); gwArr.add(gateway[2]); gwArr.add(gateway[3]);

    JsonArray dnsArr = doc["dns"].to<JsonArray>();
    dnsArr.add(dns[0]); dnsArr.add(dns[1]); dnsArr.add(dns[2]); dnsArr.add(dns[3]);

    return save_jsonDoc(doc, file);
}

bool CLASS_CORE_JSON::jsonFileLoadDoc(const String& file, JsonDocument& doc) {
    return load_jsonDoc(file, doc);
}

bool CLASS_CORE_JSON::jsonFileSaveDoc(const String& file, JsonDocument& doc) {
    return save_jsonDoc(doc, file);
}


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
