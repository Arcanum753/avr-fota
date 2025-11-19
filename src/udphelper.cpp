#include <Arduino.h>
#include <ArduinoJson.h>
#ifdef ESP32
#include <ESPmDNS.h>
#include <AsyncUDP.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncUDP.h>
#endif

#include <WiFiClient.h>
#include "udphelper.h"
#include "eertos.h"
#include "prog_isp.h"
#include "debug.h"

IPAddress responseIp ((const unsigned char *) LOCALHOST);
AsyncUDP udp_broadcast;
AsyncUDP udp_listen;

UDPBROADCAST_CLASS udpBroadcast(UDP_PORT);

UDPBROADCAST_CLASS :: UDPBROADCAST_CLASS (uint16_t portListen) {
    portRx = portListen;
}

void UDPBROADCAST_CLASS::udpInit(void) {

  	if (!load_config_UDP()) { defaultConfigUDP();  	}
	  
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
		udpTest(request);
	});

}

void UDPBROADCAST_CLASS::udpStart(uint16_t _port) {
    portRx = _port;
    //Start to listen UDP packets on port _port.
    if(udp_listen.listen(portRx)) {
      DEBUGLOG("UDP Listening on IP: %s and port %u \n\r",  WiFi.localIP().toString().c_str(), portRx);
        isStarted = true;
        udpBroadcastTimer();
        udp_listen.onPacket([](AsyncUDPPacket packet) {
          responseIp = packet.remoteIP();
#ifdef ESP32
          DEBUGLOG("UDP captured from %s, port %d, type: ", responseIp.toString().c_str(), packet.remotePort() );
#endif
#ifdef ESP8266
          DEBUGLOG("UDP captured from %s, port %d, type: ", responseIp.toString().c_str(), packet.remotePort() );
#endif
          DEBUGLOG(packet.isBroadcast() ? "Broadcast " : packet.isMulticast() ? "Multicast " : "Unicast ");
          DEBUGLOG("\r\n");
          // compare incoming data with keyword string
          if (strncmp((const char *)packet.data(), udpBroadcast.getudpKeyword().c_str(), udpBroadcast.getudpKeyword().length()) == 0) {
            udpResponse(); //response if keyword
          }
        });
    }
 }



void UDPBROADCAST_CLASS::udpStop(){
  isStarted = false;
  udp_listen.close();
}

 void  UDPBROADCAST_CLASS::udpBroadcastSend(uint16_t _port, String _strin){
    portTx = _port;
    if (isStarted == false) {return;}
    if (portTx == portRx) {
      DEBUGLOGISP("udpStringResp: portTx == portRx.\r\n");
      return;
    }

    char * _str = new char [_strin.length()+1];
    strcpy (_str, _strin.c_str());

    // DEBUGLOGISP("udpStringResp: %s \n\r", _str);
    udp_broadcast.broadcastTo(_str, portTx);
    DEBUGLOGISP("\r\n");
}


void  udpResponse( ){
    udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonBroadcast());
 }

void udpBroadcastTimer() {
  uint16_t timeout = udpBroadcast.getudpTimeOut();
  if (timeout > 60) {timeout = 60;}
  if (timeout == 0){
    SetTimerTask(udpBroadcastTimer, SEC * MINUTES);
    return;
  }
  if (timeout > 0 ){
    SetTimerTask(udpBroadcastTimer, SEC * MINUTES * timeout);
    udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonBroadcast());
  }
}

void  UDPBROADCAST_CLASS::udpTest(AsyncWebServerRequest *request) {
	udpBroadcastSend(getUpdPortTx(), udpJsonBroadcast());
}



void UDPBROADCAST_CLASS::send_udp_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "udpporttx|" 	  		+(String)_udpConfig.udpPortTx 	+ "|input\n";
	values += "udpportrx|" 	  		+(String)_udpConfig.udpPortRx 	+ "|input\n";
	values += "udptime|"   			+(String)_udpConfig.udpTimeOut 	+ "|input\n";
	values += "udpkeyword|"   		+		 _udpConfig.keyword 	+ "|input\n";
	request->send(200, "text/plain", values);
}


void UDPBROADCAST_CLASS::get_udp_configuration_html(AsyncWebServerRequest *request) {
	//DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	if (request->args() > 0) { // get new configs from args
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "udpporttx")  		{ _udpConfig.udpPortTx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpportrx")  		{ _udpConfig.udpPortRx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udptime")  			{ _udpConfig.udpTimeOut = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpkeyword")		{ _udpConfig.keyword = ESPHTTPServer.urldecode(request->arg(i));		continue; }
		}
		request->send_P(200, "text/html", Page_GeneralUdp);	// refresh page
		save_configUDP();	 	// Save Settings
		udpBroadcastTimer();	// start new UDP broadcasting
	}
	else {	ESPHTTPServer.handleFileRead(request->url(), request);	}
}


uint16_t  UDPBROADCAST_CLASS::getUpdPortTx()    {	return _udpConfig.udpPortTx;	}
uint16_t  UDPBROADCAST_CLASS::getUpdPortRx()    {	return _udpConfig.udpPortRx;	}
uint16_t  UDPBROADCAST_CLASS::getudpTimeOut() 	{	return _udpConfig.udpTimeOut;	}
String    UDPBROADCAST_CLASS::getudpKeyword() 	{	return _udpConfig.keyword;		}

String UDPBROADCAST_CLASS::udpJsonBroadcast()   {
  DEBUGLOG(__PRETTY_FUNCTION__); DEBUGLOG("\r\n");
	String _ret = "";

	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	avrprog.cfgFileStructGet( AVRISP_HexFiles_Web);
	String hostname = "http://" ;//+ _sysConfig.deviceName+"_"+_sysConfig.deviceSerial+".local";
	String iphost 	= "http://" + WiFi.localIP().toString();
	JsonDocument jsonDoc;
	// jsonDoc["deviceName"] 		= _sysConfig.deviceName;
	// jsonDoc["deviceSerial"] 	= _sysConfig.deviceSerial;
	// jsonDoc["deviceType"] 		= _sysConfig.deviceType;


	jsonDoc["ip"]           = WiFi.localIP().toString();
	jsonDoc["dnshost"] 			= hostname;
	jsonDoc["iphost"] 			= iphost;
	jsonDoc["mac"] 			    = WiFi.macAddress();
	jsonDoc["ntpNow"] 			= NTP.getTimeDateString() ;

	jsonDoc["udpPortTx"] 		= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 		= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 		= _udpConfig.udpTimeOut;
	jsonDoc["keyword"] 			= _udpConfig.keyword;

	//jsonDoc["chip"] 			= AVRISP_HexFiles_Web.avr_signature;
	// jsonDoc["size"] 			= AVRISP_HexFiles_Web.chipsize;
	// jsonDoc["project"] 			= AVRISP_HexFiles_Web.project_name;
	jsonDoc["esp8266Ver"] 		= VERSION_APP;
	jsonDoc["webVer"] 			= VERSION_WEB;
	jsonDoc["buildDate"] 		= __DATE__;
	jsonDoc["buildTime"] 		= __TIME__;

	serializeJsonPretty(jsonDoc, _ret);
	
	return _ret;
}



void UDPBROADCAST_CLASS::defaultConfigUDP() {
	// DEFAULT CONFIG UDP
	_udpConfig.udpPortTx = UDP_BROADCAST_PORT_DFLT;
	_udpConfig.udpPortRx = UDP_BROADCAST_PORT_DFLT+1;
	_udpConfig.udpTimeOut = UDP_BROADCAST_TIME_DFLT;
	_udpConfig.keyword = UDP_BROADCAST_KEYWORD_DFLT;
	save_configUDP();
}


bool UDPBROADCAST_CLASS::save_configUDP() {
	DEBUGLOG("Save config UDP \r\n");
	JsonDocument jsonDoc;
	jsonDoc["udpPortTx"] 	= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 	= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 	= _udpConfig.udpTimeOut;
	jsonDoc["udpkeyword"] 	= _udpConfig.keyword;
	return ESPHTTPServer.save_jsonDoc(jsonDoc, CONFIG_FILE_UDP);
}



bool UDPBROADCAST_CLASS::load_config_UDP() {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument jsonDoc;
	if (!ESPHTTPServer.load_jsonDoc(CONFIG_FILE_UDP, jsonDoc)){	return false;	}
#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif
	_udpConfig.udpPortTx 			= jsonDoc["udpPortTx"].as< int >();
	_udpConfig.udpPortRx 			= jsonDoc["udpPortRx"].as< int >();
	_udpConfig.udpTimeOut			= jsonDoc["udpTimeOut"].as< int >();
	_udpConfig.keyword				= jsonDoc["udpkeyword"].as<const char *>();
	DEBUGLOG("updPortTx: %d\r\n"	, _udpConfig.udpPortTx);
	DEBUGLOG("updPortRx: %d\r\n"	, _udpConfig.udpPortRx);
	DEBUGLOG("udpTimeOut: %d\r\n"	, _udpConfig.udpTimeOut);
	DEBUGLOG("keyword: %s\r\n"		, _udpConfig.keyword.c_str());
	return true;
}


