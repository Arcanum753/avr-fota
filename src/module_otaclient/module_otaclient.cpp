#include <Arduino.h>
#include <ArduinoJson.h>
#include "version.h"
#include "main.h"

#include <WiFiClient.h>
#include <core_ntp/NtpClientLib.h>
#include "core_json/core_json.h"
#include "module_otaclient/module_otaclient.h"
#include "eertos.h"
#include "common.h"
#include "module_otaclient_version.h"

// Single global object - the class itself
MODULE_CLASS_OTACLIENT otaClient;

MODULE_CLASS_OTACLIENT::MODULE_CLASS_OTACLIENT() {
    _isStarted = false;
}

uint16_t MODULE_CLASS_OTACLIENT::getTimeOut()   { return _config.timeOut; }
bool MODULE_CLASS_OTACLIENT::powerOnGet()       { return _config.powerOn; }
bool MODULE_CLASS_OTACLIENT::responseGet()       { return _config.response; }
uint8_t MODULE_CLASS_OTACLIENT::isStart()          { return _isStarted; }

void MODULE_CLASS_OTACLIENT::begin() {
    defaultConfig();
    if ( load_config() == false) {save_config();}
    DEBUGOTACLIENT("%s\r\n", __FUNCTION__);
    if (_isStarted){    return;    }
    
    _isStarted = true;
    SetTimerTask(otaclientTimer, SEC * MINUTES * otaClient.getTimeOut());
}

// ========== TIMER (for calling from other files) ==========
void otaclientTimer() {
    uint16_t timeout = otaClient.getTimeOut();
    if (otaClient.isStart() == false){   return; }
    if (timeout > 60){ timeout = 60;}
    if (timeout == 0) { return;  }
    
    DEBUGOTACLIENT("OtaClient timeout %d min. ", timeout);
    SetTimerTask(otaclientTimer, SEC * MINUTES * timeout);
    // TODO: Add periodic action here
}

// ========== TEST ==========
void MODULE_CLASS_OTACLIENT::test(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    // TODO: Add test action here
}

// ========== WEB INIT ==========
void MODULE_CLASS_OTACLIENT::webInit(void) {

    ESPHTTPServer.on(HTML_FILE_OTACLIENT, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        get_configuration_html(request);
    });
    
    ESPHTTPServer.on("/otaclient/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        send_configuration_values_html(request);
    });
    
    ESPHTTPServer.on("/otaclient/test", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        test(request);
    });

    ESPHTTPServer.on("/otaclient/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

// ========== SEND CONFIG HTML ==========
void MODULE_CLASS_OTACLIENT::send_configuration_values_html(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaclienttime|"     + String(_config.timeOut) + "|input\n";
    values += "otaclientpoweron|"  + String(_config.powerOn ? "checked" : "") + "|chk\n";
    values += "otaclientresponse|"  + String(_config.response ? "checked" : "") + "|chk\n";
    request->send(200, "text/plain", values);
}

// ========== GET CONFIG HTML ==========
void MODULE_CLASS_OTACLIENT::get_configuration_html(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    _config.powerOn  = false; 
    _config.response = false;
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGOTACLIENT("Arg %d: %s %s\r\n", i, 
                     request->argName(i).c_str(), 
                     request->arg(i).c_str());
            
            if (request->argName(i) == "otaclienttime")   { _config.timeOut = request->arg(i).toInt(); }
            if (request->argName(i) == "otaclientpoweron")  { _config.powerOn = true;  }
            if (request->argName(i) == "otaclientresponse") { _config.response = true; }
        }
        
        request->send_P(200, "text/html", Page_GeneralOtaClient);
        save_config();
        otaclientTimer();
    }
    else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ========== JSON GET ==========
String MODULE_CLASS_OTACLIENT::jsonGet() {
    String ret = "";
    JsonDocument jsonDoc;
    
    jsonDoc["deviceName"]   = ESPHTTPServer._sysConfig.deviceName;
    jsonDoc["deviceSerial"] = ESPHTTPServer._sysConfig.deviceSerial;
    
    jsonDoc["ip"]           = WiFi.localIP().toString();
    jsonDoc["mac"]          = WiFi.macAddress();
    jsonDoc["timeOut"]   = _config.timeOut;

    jsonDoc["target"]       = BUILD_ENV;
    jsonDoc["buildtime"]    = BUILD_TIME;
    jsonDoc["gitbranch"]    = GIT_BRANCH;
    jsonDoc["gitcommit"]    = GIT_COMMIT;
    jsonDoc["uptime"]       = (String)NTP.getUptimeString();
    jsonDoc["rstreason"]    =  ESPHTTPServer.getResetReason();

    jsonDoc["espVer"]       = FIRMWARE_VERSION;
    jsonDoc["webVer"]       = VERSION_WEB;
	
	serializeJsonPretty(jsonDoc, ret);
    return ret;
}

// ========== DEFAULT CONFIG ==========
void MODULE_CLASS_OTACLIENT::defaultConfig() {
    _config.timeOut = OTACLIENT_TIME_DFLT;
    _config.powerOn = OTACLIENT_POWERON;
    _config.response = OTACLIENT_RESPONSE;
}

// ========== SAVE CONFIG ==========
bool MODULE_CLASS_OTACLIENT::save_config() {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument jsonDoc;
    jsonDoc["timeOut"]   = _config.timeOut;
    jsonDoc["powerOn"]   = _config.powerOn;
    jsonDoc["response"]   = _config.response;
    return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_FILE_OTACLIENT);
}

// ========== LOAD CONFIG ==========
bool MODULE_CLASS_OTACLIENT::load_config() {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument jsonDoc;
    if (ModClassJson.load_jsonDoc(CONFIG_FILE_OTACLIENT, jsonDoc) == false) { return false; }
    
    _config.timeOut   = jsonDoc["timeOut"].as<int>();
    _config.powerOn   = jsonDoc["powerOn"].as<bool>();
    _config.response   = jsonDoc["response"].as<bool>();
    
    DEBUGOTACLIENT("timeOut: %d\n\r", _config.timeOut);
    DEBUGOTACLIENT("powerOn: %d\n\r", _config.powerOn);
    DEBUGOTACLIENT("response: %d\n\r", _config.response);
    
    return true;
}

String MODULE_CLASS_OTACLIENT::getVersionStr(){
    return String(MODULE_OTACLIENT_VERSION);
}

String MODULE_CLASS_OTACLIENT::getGeneratedTime(){
    return String(MODULE_OTACLIENT_GENERATED_TIME);
}

String MODULE_CLASS_OTACLIENT::getCommitDateStr(){
    return String(MODULE_OTACLIENT_COMMIT_DATE_STR);
}

void MODULE_CLASS_OTACLIENT::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaclientversion|"     + getVersionStr()    + "|dev\n";
    values += "otaclientgentime|"     + getGeneratedTime() + "|dev\n";
    values += "otaclientgendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}