

#include "main.h"
#include <ArduinoJson.h>


#include "FSWebServerLib.h"
#include "debug.h"
#include "core_json.h"
#include "core_json_version.h"
CORE_CLASS_JSON ModClassJson(false);

CORE_CLASS_JSON :: CORE_CLASS_JSON (bool _in) {
	dumb = _in;
}

#if defined(ESP32)
    void CORE_CLASS_JSON::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void CORE_CLASS_JSON::setFs(FS* fs)	// esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool CORE_CLASS_JSON::load_jsonDoc(const String& file, JsonDocument& jsonDoc){
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
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


bool CORE_CLASS_JSON::save_jsonDoc(const JsonDocument& jsonDoc,	const String& file) {
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
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


String CORE_CLASS_JSON::getVersionStr(){
    return String(CORE_JSON_VERSION);
}

String CORE_CLASS_JSON::getGeneratedTime(){
    return String(CORE_JSON_GENERATED_TIME);
}

String CORE_CLASS_JSON::getCommitDateStr(){
    return String(CORE_JSON_COMMIT_DATE_STR);
}