#include <Arduino.h>
#include <ArduinoJson.h>

#include "version.h"

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
#include "core_json/module_json.h"

#include "module_udp/module_udp.h"


#include "eertos.h"
#include "common.h"


IPAddress _responseIp ((const unsigned char *) LOCALHOST);
AsyncUDP udp_listen;
AsyncUDPMessage message(UDP_DATA_MESSAGE_LEGHT);

UDPBROADCAST_CLASS udpBroadcast(UDP_PORT);

UDPBROADCAST_CLASS :: UDPBROADCAST_CLASS (uint16_t portListen) {
    portRx = portListen;
}


uint16_t  UDPBROADCAST_CLASS::getUpdPortTx()    {	return _udpConfig.udpPortTx;	}
uint16_t  UDPBROADCAST_CLASS::getUpdPortRx()    {	return _udpConfig.udpPortRx;	}
uint16_t  UDPBROADCAST_CLASS::getudpTimeOut() 	{	return _udpConfig.udpTimeOut;	}
String    UDPBROADCAST_CLASS::getudpKeyword() 	{	return _udpConfig.keyword;		}
bool    UDPBROADCAST_CLASS::getudpPowerOn() 	{	return _udpConfig.udpPowerOn;		}



void UDPBROADCAST_CLASS::begin(uint16_t _port) {
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	if (isStarted == 1)	{	return;	}
    portRx = _port;
    //Start to listen UDP packets on port _port.
	SetTimerTask(udpBroadcastTimer, SEC * MINUTES * udpBroadcast.getudpTimeOut());
    if(udp_listen.listen(portRx)) {
      	DEBUGUDP("UDP Listening on IP: %s and port %u \n\r",  WiFi.localIP().toString().c_str(), portRx);
        isStarted = 1;
        
        udp_listen.onPacket(processUdpListenPacketHandler) ;
	
    }
 }

void processUdpListenPacketHandler(AsyncUDPPacket &packet) {
	// data lenght check
	if (packet.length() > UDP_DATA_LENGHT_MAX) {
		DEBUGUDP("Udp rcv data size overmax!\n\r");
		return;
	} 
	// is broadcast &
	bool _isBroadcast = packet.isBroadcast();
	if (_isBroadcast == false) {
		DEBUGUDP("Udp rcv non broadcast!\n\r");
		return;
	}
	_responseIp = packet.remoteIP();
	// get data
	char _udpDataBuf[UDP_DATA_LENGHT_MAX];
	for (int i=0; i < packet.length(); i++){
		_udpDataBuf[i] = (char)* (packet.data()+i);
	}

	// compare incoming data with keyword string
	if (strncmp(_udpDataBuf, udpBroadcast.getudpKeyword().c_str(), udpBroadcast.getudpKeyword().length()) == 0) {
		udpResponse(_responseIp); //response if keyword
	}
}

void UDPBROADCAST_CLASS::udpStop()	{
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	isStarted = 0;
	udp_listen.close();
}
		
void  udpResponse(IPAddress _Ip)	{
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	message.flush();
	message.print(udpBroadcast.udpJsonGet());
	uint16_t _portTx= udpBroadcast.getUpdPortTx();
	udp_listen.sendTo(message, _Ip, _portTx);
	
}

void  udpBroadcastSimple( ){
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonGet());
}



 void  UDPBROADCAST_CLASS::udpBroadcastSend(uint16_t _port, String _strin){
	if (isStarted == 0) {	return;	}
    portTx = _port;

    if (portTx == getUpdPortRx()) {
      DEBUGUDP("udpStringResp: portTx == portRx.\r\n");
      return;
    }
    char * _str = new char [_strin.length()+1];
    strcpy (_str, _strin.c_str());
    
	udp_listen.broadcastTo(_str, portTx);
    DEBUGUDP("  broadcastTo port: %u \n\r", portTx);
    
}

void udpBroadcastTimer() {
  uint16_t timeout = udpBroadcast.getudpTimeOut();
  if (udpBroadcast.isStart() == 0) {	return;	}
  if (timeout > 60) {	timeout = 60;	}
  if (timeout == 0)	{	return;	}
	if (timeout > 0 )	{
	  DEBUGUDP("Udp timeout %d min. ", timeout);
	  udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonGet());
	  SetTimerTask(udpBroadcastTimer, SEC * MINUTES * timeout);
  }
  
}

void  UDPBROADCAST_CLASS::udpBroadcastTest(AsyncWebServerRequest *request) {
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	udpBroadcastSend(getUpdPortTx(), udpJsonGet());
}



void UDPBROADCAST_CLASS::webInit(void) {
 	defaultConfigUDP();
  	if (!load_config_UDP()) {  save_configUDP();	}
	  
	  ESPHTTPServer.on(HTML_FILE_UDP, HTTP_POST, [this](AsyncWebServerRequest *request) {
		  if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		  get_udp_configuration_html(request);
	  });
  ESPHTTPServer.on("/udp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		send_udp_configuration_values_html(request);
	});
	ESPHTTPServer.on("/udp/test", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		udpBroadcastTest(request);
	});

}


void UDPBROADCAST_CLASS::send_udp_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	DEBUGUDP(__FUNCTION__);	DEBUGUDP("\r\n");
	String values = "";
	values += "udpporttx|" 	  		+(String)_udpConfig.udpPortTx 	+ "|input\n";
	values += "udpportrx|" 	  		+(String)_udpConfig.udpPortRx 	+ "|input\n";
	values += "udptime|"   			+(String)_udpConfig.udpTimeOut 	+ "|input\n";
	values += "udpkeyword|"   		+		 _udpConfig.keyword 	+ "|input\n";
	values += "udppoweron|" 		+(String)(_udpConfig.udpPowerOn ? "checked" : "") + "|chk\n";
	request->send(200, "text/plain", values);
}

void UDPBROADCAST_CLASS::get_udp_configuration_html(AsyncWebServerRequest *request) {
	DEBUGUDP(__PRETTY_FUNCTION__);	DEBUGUDP("\r\n");
	if (request->args() > 0) { // get new configs from args
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGUDP("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "udpporttx")		{ _udpConfig.udpPortTx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpportrx")		{ _udpConfig.udpPortRx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udptime")		{ _udpConfig.udpTimeOut = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpkeyword")	{ _udpConfig.keyword    = urldecode(request->arg(i));		continue; }
			if (request->argName(i) == "udppoweron")	{ _udpConfig.udpPowerOn = true;		continue; }
			
		}
		request->send_P(200, "text/html", Page_GeneralUdp);	// refresh page
		save_configUDP();	 	// Save Settings
		udpBroadcastTimer();	// start new UDP broadcasting
	}
	else {	ESPHTTPServer.handleFileRead(request->url(), request);	}
}



uint8_t UDPBROADCAST_CLASS::isStart()   {
	return  isStarted;
}

String UDPBROADCAST_CLASS::udpJsonGet()   {
  	// DEBUGUDP(__PRETTY_FUNCTION__); DEBUGUDP("\r\n");
	String _ret = "";
	JsonDocument jsonDoc;

	// AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	// avrprog.cfgFileStructGet( AVRISP_HexFiles_Web);
	
	// String iphost 	= "http://" + WiFi.localIP().toString();
	// String hostname = "http://" + _sysConfig.deviceName+"_"+_sysConfig.deviceSerial+".local";
	
	jsonDoc["deviceName"] 		= ESPHTTPServer._sysConfig.deviceName;
	jsonDoc["deviceSerial"]		= ESPHTTPServer._sysConfig.deviceSerial;
	jsonDoc["deviceType"] 		= ESPHTTPServer._sysConfig.deviceType;
	jsonDoc["ip"]           	= WiFi.localIP().toString();
	// jsonDoc["dnshost"] 			= hostname;
	// jsonDoc["iphost"] 			= iphost;
	jsonDoc["mac"] 			    = WiFi.macAddress();
	// jsonDoc["ntpNow"] 			= NTP.getTimeDateString() ;

	jsonDoc["udpPortTx"] 		= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 		= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 		= _udpConfig.udpTimeOut;
	jsonDoc["udpPowerOn"] 		= _udpConfig.udpPowerOn;
	jsonDoc["keyword"] 			= _udpConfig.keyword;

	jsonDoc["espVer"]	 		= FIRMWARE_VERSION;
	jsonDoc["webVer"] 			= VERSION_WEB;
	jsonDoc["buildDate"] 		= __DATE__;
	jsonDoc["buildTime"] 		= __TIME__;
	
	// jsonDoc["chip"] 			= AVRISP_HexFiles_Web.avr_signature;
	// jsonDoc["size"] 			= AVRISP_HexFiles_Web.chipsize;
	// jsonDoc["project"] 			= AVRISP_HexFiles_Web.project_name;
	serializeJsonPretty(jsonDoc, _ret);
	
	return _ret;
}



void UDPBROADCAST_CLASS::defaultConfigUDP() {
	// DEFAULT CONFIG UDP
	_udpConfig.udpPortTx = UDP_BROADCAST_PORT_DFLT;
	_udpConfig.udpPortRx = UDP_BROADCAST_PORT_DFLT+1;
	_udpConfig.udpTimeOut = UDP_BROADCAST_TIME_DFLT;
	_udpConfig.keyword = UDP_BROADCAST_KEYWORD_DFLT;
	_udpConfig.udpPowerOn = UDP_BROADCAST_POWERON;
	
}


bool UDPBROADCAST_CLASS::save_configUDP() {
	DEBUGUDP(__PRETTY_FUNCTION__);	DEBUGUDP("\r\n");
	JsonDocument jsonDoc;
	jsonDoc["udpPortTx"] 	= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 	= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 	= _udpConfig.udpTimeOut;
	jsonDoc["udpkeyword"] 	= _udpConfig.keyword;
	jsonDoc["udpPowerOn"] 	= _udpConfig.udpPowerOn;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_FILE_UDP);
}



bool UDPBROADCAST_CLASS::load_config_UDP() {
	DEBUGUDP(__PRETTY_FUNCTION__);	DEBUGUDP("\r\n");
	JsonDocument jsonDoc;
	if (!ModClassJson.load_jsonDoc(CONFIG_FILE_UDP, jsonDoc))	{	return false;	}
// #ifndef RELEASE
	// String temp;
	// serializeJsonPretty(jsonDoc, temp);
	// Serial.println(temp.c_str());
// #endif
	_udpConfig.udpPortTx 			= jsonDoc["udpPortTx"].as< int >();
	_udpConfig.udpPortRx 			= jsonDoc["udpPortRx"].as< int >();
	_udpConfig.udpTimeOut			= jsonDoc["udpTimeOut"].as< int >();
	_udpConfig.keyword				= jsonDoc["udpkeyword"].as<const char *>();
	_udpConfig.udpPowerOn			= jsonDoc["udpPowerOn"].as< bool >(); 
	DEBUGUDP("updPortTx: %d\r\n"	, _udpConfig.udpPortTx);
	DEBUGUDP("updPortRx: %d\r\n"	, _udpConfig.udpPortRx);
	DEBUGUDP("udpTimeOut: %d\r\n"	, _udpConfig.udpTimeOut);
	DEBUGUDP("keyword: %s\r\n"		, _udpConfig.keyword.c_str());
	DEBUGUDP("udpPowerOn: %d\r\n"	, _udpConfig.udpPowerOn);
	return true;
}


