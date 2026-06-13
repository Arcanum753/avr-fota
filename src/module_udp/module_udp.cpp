#include <Arduino.h>
#include "version.h"
#include "main.h"
#ifdef ESP32
#include <ESPmDNS.h>
#include <AsyncUDP.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncUDP.h>
#endif

#include <WiFiClient.h>
#include <core_ntp/NtpClientLib.h>
#include "core_json/core_json.h"
#include "module_udp/module_udp.h"
#include "eertos.h"
#include "common.h"
#include "module_udp_version.h"

// Единственный глобальный объект - сам класс
UDPBROADCAST_CLASS udpBroadcast(UDP_PORT);

UDPBROADCAST_CLASS::UDPBROADCAST_CLASS(uint16_t portListen) {
    _portRx = portListen;
    _portTx = portListen + 1;
    _isStarted = false;
    _isSending = false;
    _lastSendTime = 0;
    
}

uint16_t UDPBROADCAST_CLASS::getUpdPortTx()    { return _udpConfig.udpPortTx; }
uint16_t UDPBROADCAST_CLASS::getUpdPortRx()    { return _udpConfig.udpPortRx; }
uint16_t UDPBROADCAST_CLASS::getudpTimeOut()   { return _udpConfig.udpTimeOut; }
String UDPBROADCAST_CLASS::getudpKeyword()     { return _udpConfig.keyword; }
bool UDPBROADCAST_CLASS::udpPowerOnGet()       { return _udpConfig.udpPowerOn; }
bool UDPBROADCAST_CLASS::udpResponseGet()       { return _udpConfig.udpResponse; }
uint8_t UDPBROADCAST_CLASS::isStart()          { return _isStarted; }

void UDPBROADCAST_CLASS::begin() {
    defaultConfigUDP();
    if ( load_config_UDP() == false) {save_configUDP();}
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (_isStarted){    return;    }
    
    _portRx = _udpConfig.udpPortRx;
    
    if (_udp.listen(_portRx) == true) {
        DEBUGUDP("UDP Listening on IP: %s and port %u\n\r",  WiFi.localIP().toString().c_str(), _portRx);
        _isStarted = true;
        _udp.onPacket(processUdpListenPacket);
    }
    SetTimerTask(udpBroadcastTimer, SEC * MINUTES * udpBroadcast.getudpTimeOut());
}

// ========== STOP ==========
void UDPBROADCAST_CLASS::udpStop() {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    _isStarted = false;
    _udp.close();
}

// ========== SEND BROADCAST ==========
void UDPBROADCAST_CLASS::udpBroadcastSend(uint16_t _port, String _strin) {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (_isStarted == false) {  return; }
    if (_strin.length() == 0 || _strin.length() >= UDP_DATA_MESSAGE_LEN) {  return; }
    if (_port == getUpdPortRx()) { return; }
    
    // Проверка на зависшую отправку
    if (_isSending) {
        if (millis() - _lastSendTime > SEND_TIMEOUT) { _isSending = false; } 
        else { return; }
    }
    
    _isSending = true;
    _lastSendTime = millis();
    
    strlcpy(_sendBuffer, _strin.c_str(), sizeof(_sendBuffer));
    _sendBuffer[sizeof(_sendBuffer) - 1] = '\0';
    
    // ---- ОБХОД БАГА БИБЛИОТЕКИ ----
    // Создаем буфер в RAM и копируем туда данные принудительно
    char* ramBuffer = (char*)malloc(_strin.length() + 1);
    if (ramBuffer) {
        strcpy(ramBuffer, _sendBuffer);
        
        // Отправляем через AsyncUDPMessage
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(ramBuffer);
        _udp.broadcastTo(msg, _port);
        
        free(ramBuffer);
    } else {
        // Если не удалось выделить память, отправляем как обычно
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(_sendBuffer);
        _udp.broadcastTo(msg, _port);
    }
    // --------------------------------
    _isSending = false;
}

void processUdpListenPacket(AsyncUDPPacket &packet) {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (packet.length() > UDP_DATA_LENGHT_MAX) { return; } // если пакет длинный
    
    if (udpBroadcast.udpResponseGet() == false) { return; } // если мы не должны отвечать
    
    if (packet.isBroadcast() == false) { return; } // если пакет не бродкаст
    
    udpBroadcast._responseIp = packet.remoteIP();
    
    // Get data
    char udpDataBuf[UDP_DATA_LENGHT_MAX + 1]; // +1 для нуль-терминатора
    size_t dataLen = packet.length();
    for (size_t i = 0; i < dataLen; i++) {
        udpDataBuf[i] = (char)*(packet.data() + i);
    }
    udpDataBuf[dataLen] = '\0'; // Важно!
    
    // Compare with keyword
    if (strncmp(udpDataBuf, udpBroadcast.getudpKeyword().c_str(), udpBroadcast.getudpKeyword().length()) == 0) {
        udpResponseHandler(udpBroadcast._responseIp);
    }
}

// ========== RESPONSE HANDLER ==========
void udpResponseHandler(IPAddress ip) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    
    // Создаем локальное сообщение для каждого ответа
    AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
    msg.print(udpBroadcast.udpJsonGet());
    
    uint16_t portTx = udpBroadcast.getUpdPortTx();
    udpBroadcast._udp.sendTo(msg, ip, portTx);
}

// ========== SIMPLE BROADCAST (для вызова из других файлов) ==========
void udpBroadcastSimple() {
    udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonGet());
}

// ========== TIMER (для вызова из других файлов) ==========
void udpBroadcastTimer() {
    uint16_t timeout = udpBroadcast.getudpTimeOut();
    if (udpBroadcast.isStart() == false){   return; }
    if (timeout > 60){ timeout = 60;}
    if (timeout == 0) { return;  }
    
    DEBUGUDP("Udp timeout %d min. ", timeout);
    SetTimerTask(udpBroadcastTimer, SEC * MINUTES * timeout);
    udpBroadcastSimple();
}

// ========== TEST ==========
void UDPBROADCAST_CLASS::udpBroadcastTest(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    udpBroadcastSend(getUpdPortTx(), udpJsonGet());
}

// ========== WEB INIT ==========
void UDPBROADCAST_CLASS::webInit(void) {

    
    ESPHTTPServer.on(HTML_FILE_UDP, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        get_udp_configuration_html(request);
    });
    
    ESPHTTPServer.on("/udp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        send_udp_configuration_values_html(request);
    });
    
    ESPHTTPServer.on("/udp/test", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        udpBroadcastTest(request);
    });

    ESPHTTPServer.on("/udp/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

// ========== SEND CONFIG HTML ==========
void UDPBROADCAST_CLASS::send_udp_configuration_values_html(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "udpporttx|"   + String(_udpConfig.udpPortTx) + "|input\n";
    values += "udpportrx|"   + String(_udpConfig.udpPortRx) + "|input\n";
    values += "udptime|"     + String(_udpConfig.udpTimeOut) + "|input\n";
    values += "udpkeyword|"  + _udpConfig.keyword + "|input\n";
    values += "udppoweron|"  + String(_udpConfig.udpPowerOn ? "checked" : "") + "|chk\n";
    values += "udpresponse|"  + String(_udpConfig.udpResponse ? "checked" : "") + "|chk\n";
    request->send(200, "text/plain", values);
}

// ========== GET CONFIG HTML ==========
void UDPBROADCAST_CLASS::get_udp_configuration_html(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    _udpConfig.udpPowerOn  = false; 
    _udpConfig.udpResponse = false;
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGUDP("Arg %d: %s %s\r\n", i, 
                     request->argName(i).c_str(), 
                     request->arg(i).c_str());
            
            if (request->argName(i) == "udpporttx") { _udpConfig.udpPortTx = request->arg(i).toInt(); }
            if (request->argName(i) == "udpportrx") { _udpConfig.udpPortRx = request->arg(i).toInt(); }
            if (request->argName(i) == "udptime")   { _udpConfig.udpTimeOut = request->arg(i).toInt(); }
            if (request->argName(i) == "udpkeyword") { _udpConfig.keyword = urldecode(request->arg(i)); }
            if (request->argName(i) == "udppoweron")  { _udpConfig.udpPowerOn = true;  }
            if (request->argName(i) == "udpresponse") { _udpConfig.udpResponse = true; }
        }
        
        request->send_P(200, "text/html", Page_GeneralUdp);
        save_configUDP();
        udpBroadcastTimer();
    }
    else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ========== JSON GET ==========
String UDPBROADCAST_CLASS::udpJsonGet() {
    String ret = "";
    ret += "{\n";
    ret += "  \"deviceName\": \"" + ESPHTTPServer._sysConfig.deviceName + "\",\n";
    ret += "  \"deviceSerial\": \"" + ESPHTTPServer._sysConfig.deviceSerial + "\",\n";
    ret += "  \"ip\": \"" + WiFi.localIP().toString() + "\",\n";
    ret += "  \"mac\": \"" + WiFi.macAddress() + "\",\n";
    ret += "  \"udpPortTx\": " + String(_udpConfig.udpPortTx) + ",\n";
    ret += "  \"udpPortRx\": " + String(_udpConfig.udpPortRx) + ",\n";
    ret += "  \"udpTimeOut\": " + String(_udpConfig.udpTimeOut) + ",\n";
    ret += "  \"keyword\": \"" + _udpConfig.keyword + "\",\n";
    ret += "  \"target\": \"" + String(BUILD_ENV) + "\",\n";
    ret += "  \"buildtime\": \"" + String(BUILD_TIME) + "\",\n";
    ret += "  \"gitbranch\": \"" + String(GIT_BRANCH) + "\",\n";
    ret += "  \"gitcommit\": \"" + String(GIT_COMMIT) + "\",\n";
    ret += "  \"uptime\": \"" + String(NTP.getUptimeString()) + "\",\n";
    ret += "  \"rstreason\": \"" + ESPHTTPServer.getResetReason() + "\",\n";
    ret += "  \"espVer\": \"" + String(FIRMWARE_VERSION) + "\"\n";
    ret += "}\n";
    return ret;
}

// ========== DEFAULT CONFIG ==========
void UDPBROADCAST_CLASS::defaultConfigUDP() {
    _udpConfig.udpPortTx = UDP_BROADCAST_PORT_DFLT;
    _udpConfig.udpPortRx = UDP_BROADCAST_PORT_DFLT + 1;
    _udpConfig.udpTimeOut = UDP_BROADCAST_TIME_DFLT;
    _udpConfig.keyword = UDP_BROADCAST_KEYWORD_DFLT;
    _udpConfig.udpPowerOn = UDP_BROADCAST_POWERON;
    _udpConfig.udpResponse = UDP_BROADCAST_RESPONSE;
}

// ========== SAVE CONFIG ==========
bool UDPBROADCAST_CLASS::save_configUDP() {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    if (!ModClassJson.jsonFileWriteInt(CONFIG_FILE_UDP, "udpPortTx", _udpConfig.udpPortTx)) return false;
    if (!ModClassJson.jsonFileWriteInt(CONFIG_FILE_UDP, "udpPortRx", _udpConfig.udpPortRx)) return false;
    if (!ModClassJson.jsonFileWriteInt(CONFIG_FILE_UDP, "udpTimeOut", _udpConfig.udpTimeOut)) return false;
    if (!ModClassJson.jsonFileWriteStr(CONFIG_FILE_UDP, "udpkeyword", _udpConfig.keyword)) return false;
    if (!ModClassJson.jsonFileWriteBool(CONFIG_FILE_UDP, "udpPowerOn", _udpConfig.udpPowerOn)) return false;
    if (!ModClassJson.jsonFileWriteBool(CONFIG_FILE_UDP, "udpResponse", _udpConfig.udpResponse)) return false;
    return true;
}

// ========== LOAD CONFIG ==========
bool UDPBROADCAST_CLASS::load_config_UDP() {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    int32_t portTx = 0, portRx = 0, timeout = 0;
    if (!ModClassJson.jsonFileReadInt(CONFIG_FILE_UDP, "udpPortTx", portTx)) return false;
    ModClassJson.jsonFileReadInt(CONFIG_FILE_UDP, "udpPortRx", portRx);
    ModClassJson.jsonFileReadInt(CONFIG_FILE_UDP, "udpTimeOut", timeout);
    _udpConfig.udpPortTx = (int)portTx;
    _udpConfig.udpPortRx = (int)portRx;
    _udpConfig.udpTimeOut = (int)timeout;
    ModClassJson.jsonFileReadStr(CONFIG_FILE_UDP, "udpkeyword", _udpConfig.keyword);
    ModClassJson.jsonFileReadBool(CONFIG_FILE_UDP, "udpPowerOn", _udpConfig.udpPowerOn);
    ModClassJson.jsonFileReadBool(CONFIG_FILE_UDP, "udpResponse", _udpConfig.udpResponse);
    
    DEBUGUDP("updPortTx: %d\n\r", _udpConfig.udpPortTx);
    DEBUGUDP("updPortRx: %d\n\r", _udpConfig.udpPortRx);
    DEBUGUDP("udpTimeOut: %d\n\r", _udpConfig.udpTimeOut);
    DEBUGUDP("keyword: %s\n\r", _udpConfig.keyword.c_str());
    DEBUGUDP("udpPowerOn: %d\n\r", _udpConfig.udpPowerOn);
    DEBUGUDP("udpResponse: %d\n\r", _udpConfig.udpResponse);
    
    return true;
}

String UDPBROADCAST_CLASS::getVersionStr(){
    return String(MODULE_UDP_VERSION);
}

String UDPBROADCAST_CLASS::getGeneratedTime(){
    return String(MODULE_UDP_GENERATED_TIME);
}

String UDPBROADCAST_CLASS::getCommitDateStr(){
    return String(MODULE_UDP_COMMIT_DATE_STR);
}

void UDPBROADCAST_CLASS::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "udpversion|"     + getVersionStr()    + "|dev\n";
    values += "udpgentime|"     + getGeneratedTime() + "|dev\n";
    values += "udpgendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}