#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif


#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "FSWebServerLib.h"
#include "module_ota.h"
#include "common.h"

MODULE_OTA_CLASS modOtaClass(false);

MODULE_OTA_CLASS :: MODULE_OTA_CLASS (bool _in) {
	 dumb = _in;
 }
 
#if ESP32
    void MODULE_OTA_CLASS::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void MODULE_OTA_CLASS::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

 void MODULE_OTA_CLASS::begin(String _hostname, String _password){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	prepareSizesForUpdate();
	ConfigureOTA(_hostname, _password);
 }

void MODULE_OTA_CLASS::prepareSizesForUpdate (){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	freeSketchSpace  = ESP.getFreeSketchSpace();
}



bool  MODULE_OTA_CLASS::ConfigureOTA( String _hostname, String _password) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	
	// No authentication by default
	if (_hostname != "") {
	ArduinoOTA.setHostname(_hostname.c_str());
	DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else {
		return false;
	}

	if (_password != "") {
		ArduinoOTA.setPassword(_password.c_str());
		DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else {
		return false;
	}	


#ifndef RELEASE
	ArduinoOTA.onStart([]() {
		DEBUGOTA("\r\n ArduinoOTA start. \r\n");
	});

#if defined(ESP32)
	ArduinoOTA.onEnd(std::bind([](fs::SPIFFSFS* fs)
#elif defined(ESP8266)
	ArduinoOTA.onEnd(std::bind([](FS* fs)
#endif
	{
		fs->end();
		DEBUGOTA("\r\n ArduinoOTA end. \r\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUGOTA("\t OTA Progress: %u%% \r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUGOTA("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) 			{DEBUGOTA("Auth Failed\r\n");		}
		else if (error == OTA_BEGIN_ERROR) 		{DEBUGOTA("Begin Failed\r\n");		}
		else if (error == OTA_CONNECT_ERROR)	{DEBUGOTA("Connect Failed\r\n");	}
		else if (error == OTA_RECEIVE_ERROR) 	{DEBUGOTA("Receive Failed\r\n");	}
		else if (error == OTA_END_ERROR) 		{DEBUGOTA("End Failed\r\n");		}
	});
	DEBUGOTA("\r\n ArduinoOTA Ready \r\n");
#endif // RELEASE
	ArduinoOTA.begin();

	return true;
}


 void MODULE_OTA_CLASS::loopHandler(){
	 ArduinoOTA.handle();
 }


 void MODULE_OTA_CLASS::webInit() {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	//update.html vvv
		ESPHTTPServer.on("/update/updatepossible", [this](AsyncWebServerRequest *request) {
			 if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
			 send_update_firmware_values_html(request);
		});
		ESPHTTPServer.on("/setmd5", [this](AsyncWebServerRequest *request) {
			 if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
			 setUpdateMD5(request);
		});
		ESPHTTPServer.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
			if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
			if (!ESPHTTPServer.handleFileRead("/update.html", request)) { request->send(404, "text/plain", "FileNotFound");	}
		});

		ESPHTTPServer.on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
			//what do when we finish
			 if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
			 updateFileExecute (request);
		}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
			// uploading
			 uploadUpdateFile(request, filename, index, data, len, final);
		});
	//update.html ^^^

 }



 
void MODULE_OTA_CLASS::uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	String	values = "";
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	static long totalSize = 0;
	int updatePartition = 1;
	if (!index) { //UPLOAD_FILE_START
		if (_fs) { _fs->end(); }//SPIFFS.end();
		//Update.runAsync(true);
		uint32_t maxSketchSpace = ESP.getSketchSize();
		DEBUGOTA("Update start: %s\r\n", filename.c_str());
		DEBUGOTA("Max free scketch space: %u\r\n", maxSketchSpace);
		DEBUGOTA("New scketch size: %u\r\n", _updateFileSize);
		if (_browserFileMD5 != NULL && _browserFileMD5 != "") {
			Update.setMD5(_browserFileMD5.c_str());
			DEBUGOTA("Hash from btowser: %s\r\n", _browserFileMD5.c_str());
		} else {
			values += "OTA Update error no md4 hash!" ;
			request->send(500, "text/plain", values);
			return ;
		}
		#if defined(ESP32)
		if (typeOTAfile == FILESYSTEM) 	{ updatePartition = U_SPIFFS; }
		#elif defined(ESP8266)
		if (typeOTAfile == FILESYSTEM) 	{ updatePartition = U_FS; }
		#endif

		if (typeOTAfile == FIRMWARE) 	{ updatePartition = U_FLASH; }

		if (!Update.begin(_updateFileSize, updatePartition)) {	//start with max available size
#ifdef DEBUG_OTA
			Update.printError(DEBUGOTASER);
#endif
			Update.end();
			values += "OTA Update error at begin" ;
			request->send(500, "text/plain", values);
			return ;
		}
		if (typeOTAfile == UNSUPPORTED || updatePartition == 1) {
			values += "OTA Update error UNSUPPORTED file!" ;
			request->send(500, "text/plain", values);
			return ;
		}
	}
	// Get upload file, continue if not start
	totalSize += len;
	//percernt formula
	uint16_t percentLoaded = (totalSize * 100) /  _updateFileSize ;
	if (  (percentLoaded % 5) == 0  && (percentLoaded != percentLoadedPrev)) {
		percentLoadedPrev = percentLoaded;
		DEBUGOTA("Uploaded: %d bytes  %u %%\r\n", totalSize, percentLoaded);
	}

	size_t written = Update.write(data, len);
	if (written != len) {
		values += "OTA Update error data load! len = " + (String)len + "written = "+ (String)written + "totalSize ="+ (String)totalSize +" \r\n";
		DEBUGOTA(values.c_str());
		request->send(500, "text/plain", values);
		return ;
	}
	if (final) {  // UPLOAD_FILE_END
		String updateHash;
		DEBUGOTA("Applying update...");
		if (Update.end(true)) { //true to set the size to the current progress
			updateHash = Update.md5String();
			DEBUGOTA("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
			DEBUGOTA("Update Success: %u\nRebooting...\r\n", request->contentLength());
		} else {
			updateHash = Update.md5String();
			DEBUGOTA("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());

#ifdef DEBUG_OTA
			Update.printError(DEBUGOTASER);
#endif
		}
	}

	//delay(2); //TODO da fuck?!
}






void MODULE_OTA_CLASS::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	String values = "";
	String updateOKstr = "";
	String updateFiletype = "";
	typeOTAfile = UNSUPPORTED;

	if (_updateFileName == OTA_FILENAME_FIRMWARE) {
		typeOTAfile = FIRMWARE;
		updateFiletype = OTA_FIRMWARE;
	}
	if (_updateFileName == OTA_FILENAME_FILESYSTEM) {
		typeOTAfile = FILESYSTEM;
		updateFiletype = OTA_FILESYSTEM;
	}
	if (typeOTAfile == UNSUPPORTED) {	updateFiletype = OTA_UNSUPPORTED;	}

	bool updateOK = maxSketchSpace < freeSketchSpace;
	if (updateOK == true) {	updateOKstr = "OK" ; } 
		else {	updateOKstr = "ERROR" ;	}

	DEBUGOTA("--updateOK: %s\r\n", updateOKstr);
	DEBUGOTA("--FreeSketchSpace: %d\r\n", freeSketchSpace);
	DEBUGOTA("--MaxSketchSpace: %d\r\n", maxSketchSpace);
	DEBUGOTA("--UpdateFiletype: %d\r\n", updateFiletype);

	values += "upd|" 			+ updateOKstr 				+ "|div\n";
	values += "updSizeFree|" 	+ (String)freeSketchSpace 	+ "|div\n";
	values += "updSizeMax|" 	+ (String)maxSketchSpace  	+ "|div\n";
	values += "updFileType|" 	+ updateFiletype		  	+ "|div\n";
	request->send(200, "text/plain", values);
}

void MODULE_OTA_CLASS::setUpdateMD5(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	_browserFileMD5 = "";
	DEBUGOTA("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  {// Read hash
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGOTA("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserFileMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserFileMD5.c_str());
				continue;
			}
			if (request->argName(i) == "size") {
				_updateFileSize = request->arg(i).toInt();
				DEBUGOTA("Update size: %d \r\n", _updateFileSize);
				continue;
			}
			if (request->argName(i) == "name") {
				_updateFileName = request->arg(i).c_str();
				DEBUGOTA("Update filename: %s \r\n", _updateFileName.c_str());
				continue;
			}
		}
		request->send(200, "text/html", "OK --> MD5: " + _browserFileMD5);
	}

}


void MODULE_OTA_CLASS::updateFileExecute (AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
	response->addHeader("Connection", "close");
	response->addHeader("Access-Control-Allow-Origin", "*");
	request->send(response);
	if (this->_fs) { this->_fs->end(); }//this->_fs->end();
	ESPHTTPServer.restart_esp();

}


























