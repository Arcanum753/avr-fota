#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "main.h"

#ifdef ESP32
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include "FSWebServerLib.h"
#include "programmer.h"
#include "prog_isp.h"
#include "prog_swd.h"
#include "debug.h"


ESP_Programmer espProgrammer(0);
ESP_Programmer::ESP_Programmer(uint8_t in): _in(in){ }

#if ESP32
    void ESP_Programmer::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void ESP_Programmer::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool ESP_Programmer::begin (){
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {         cfg_FileSave();    }
	if (progType == DEVTYPE_AVR) {
		avrprog.setReset(false);  // let the AVR chip run by level up RST pin.
		avrprog.begin(); 		  // load AVR isp cfg's
	}
	if (progType == DEVTYPE_SWD) {	swdprog.stm32Fx_begin();	}
    return true;
}

int ESP_Programmer::cfg_FileSaveFromWeb(Prog_CfgFile_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	_Prog_CfgFile	=  _inStruct;
	int _ret =  (int)cfg_FileSave();
	return _ret ;
}

int  ESP_Programmer::cfg_FileStructGet(Prog_CfgFile_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    progerr_t _ret = ERROR_OK;
    if(!cfg_FileLoad()) {  return ERR_CFG; }
    _inStruct = _Prog_CfgFile;
    return _ret ;
}

void ESP_Programmer::cfg_SetDefault() {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	// _Prog_CfgFile.programmer_type	= DEFAULT_PROG_TYPE;
    _Prog_CfgFile.project_name  	= DEFAULT_PROG_PROJNAME;
    _Prog_CfgFile.chip_size      	= DEFAULT_chipsize;
}

bool ESP_Programmer::cfg_FileLoad() {
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_PROG_JSON, jsonDoc)){	return false;	}
	// _Prog_CfgFile.programmer_type	= jsonDoc["type"].as<const char *>();
    _Prog_CfgFile.project_name		= jsonDoc["project"].as<const char *>();
    _Prog_CfgFile.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
	return true;
}

bool ESP_Programmer::cfg_FileSave(){
	DEBUGLOG("Save config PROJ\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= _Prog_CfgFile.programmer_type;
    jsonDoc["project"]		= _Prog_CfgFile.project_name;
    jsonDoc["chipsize"]     = _Prog_CfgFile.chip_size;
	return save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}

String ESP_Programmer::formatBytes(size_t bytes) {
	if (bytes < 1024) 					{	return String(bytes) + "B";	}
	else
	if (bytes < (1024 * 1024))			{	return String(bytes / 1024.0) + "KB";	}
	else
	if (bytes < (1024 * 1024 * 1024))	{	return String(bytes / 1024.0 / 1024.0) + "MB";	}
	else	{	return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";	}
}

bool ESP_Programmer::web_GetDiskInfo(String &_str)	{
	bool _ret = true;

	String values 	= 	"";
	size_t sizeAll	=	0;
	size_t sizeUsed	=	0;
	#if ESP32
	 sizeAll	=	_fs->totalBytes();
	 sizeUsed	=	_fs->usedBytes();
	#elif defined(ESP8266)

	#endif

	size_t sizeFree = 0;

	if (sizeAll > sizeUsed ){ sizeFree = sizeAll - sizeUsed;	}

	values	+= "diskall|"	+ (String)(sizeAll)		+"|div\n";
	values	+= "diskused|"	+ (String)(sizeUsed) 	+"|div\n";
	values	+= "diskfree|"	+ (String)(sizeFree) 	+"|div\n";
	_str = values;
	return _ret;
}

bool ESP_Programmer::web_GetFileList(String &_str)	{
	bool _ret = true;

	size_t pos ;
	std::string fname = "";
	std::string ftype = "";
    uint32_t i = 0;

	// AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	// int _res = avrprog.cfgFileStructGet( AVRISP_HexFiles_Web	);

	String json = "[";
#if defined(ESP8266)
    if (!_fs) { _fs->begin();  }// If SPIFFS is not started
    Dir files = _fs->openDir("/");
    while (files.next()) {
        fname = files.fileName().c_str() ;
        pos = fname.find_last_of(FILE_TYPE_COMMA);
        ftype = fname.substr(pos + 1);
		if ((ftype == FILE_TYPE_HEX) || (ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)  ) {
			size_t fsize = files.fileSize();
            if (i) json += ",";
			json += "{";
			json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
			json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
			json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
			json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
			json += ",\"progchip\":\"";									json += "\"";
			json += ",\"progactual\":\"";								json += "\"";
			json += ",\"progdate\":\"";									json += "\"";
			json += "}";
			i++;
        }
    }
#endif
#if defined(ESP32)
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
    File root =  _fs->open("/");
    File files = root.openNextFile();
    while (files) {
        fname = files.name() ;
        pos = fname.find_last_of(FILE_TYPE_COMMA);
        ftype = fname.substr(pos + 1);
        if ((ftype == FILE_TYPE_HEX) || (ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)  ) {
			size_t fsize = files.size();
			if (i) json += ",";
			json += "{";
			json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
			json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
			json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
			json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
			json += ",\"progchip\":\"";									json += "\"";
			json += ",\"progactual\":\"";								json += "\"";
			json += ",\"progdate\":\"";									json += "\"";
			json += "}";
			i++;
        }
        files = root.openNextFile();
    }
#endif
	json += "]";

	_str = json;
	return _ret;
}

bool ESP_Programmer::save_jsonDoc(const JsonDocument& jsonDoc,	const String& file) {
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
	File configFile  = _fs->open(file, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open config file for writing\r\n");
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
bool ESP_Programmer::load_jsonDoc(const String& file,	JsonDocument& jsonDoc){
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
	File configFile = _fs->open(file, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUGLOG("Config file size is too large");
	configFile.close();
	return false;
	}*/
	char * buf = (char *) malloc(size);
	if ( buf == NULL){	return false;	}
	DEBUGLOGFH("File: %s, size: %d\r\n", file.c_str(), size);
	configFile.readBytes(buf, size);
	configFile.close();
	auto error = deserializeJson(jsonDoc, buf);
	free(buf);
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	// String temp;
	// serializeJsonPretty(jsonDoc, temp);
	// Serial.println(temp);
#endif
	return true;
}

void ESP_Programmer::prog_ProgTypeSet(String _str){
	progType = _str;
}

int  ESP_Programmer::prog_Programm(String _path, String _fwTime)	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
	DEBUGLOGISP(" file %s time %s \r\n", _path.c_str(), _fwTime.c_str());
	int _res = ERR_OPENFILE;

	if (progType == DEVTYPE_AVR) {
		_res  = avrprog.avr_ChipProgrammMain(_path, _fwTime );
		DEBUGLOG("avrProg  %d \n\r", _res);
	}
	if (progType == DEVTYPE_SWD) {
		_res  = swdprog.stm32_ChipProgrammMain(_path );
	}


	DEBUGLOGISP("Programming end \r\n");
	return _res;
}