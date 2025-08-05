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

#include "main.h"
#include <WiFiClient.h>



#include "main.h"
#include "udphelper.h"
#include "eertos.h"

#include "FSWebServerLib.h"
#include "debug.h"

IPAddress responseIp ((const unsigned char *) LOCALHOST);
AsyncUDP udp_broadcast;
AsyncUDP udp_listen;

UDPBROADCAST_CLASS udpBroadcast(UDP_PORT);

UDPBROADCAST_CLASS :: UDPBROADCAST_CLASS (uint16_t portListen) {
  portRx = portListen;
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

          // compare incoming data with keyword string
          if (strncmp((const char *)packet.data(), ESPHTTPServer.getudpKeyword().c_str(), ESPHTTPServer.getudpKeyword().length()) == 0) {
            udpResponse(responseIp); //response if keyword
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

void  udpResponse(IPAddress respIp ){
    udpBroadcast.udpBroadcastSend(ESPHTTPServer.getUpdPortTx(), ESPHTTPServer.udpJsonBroadcast());
 }

void udpBroadcastTimer() {
  uint16_t timeout = ESPHTTPServer.getudpTimeOut();
  if (timeout > 60) {timeout = 60;}
  if (timeout == 0){
    SetTimerTask(udpBroadcastTimer, SEC * MIN);
    return;
  } 
  if (timeout > 0 ){
    SetTimerTask(udpBroadcastTimer, SEC * MIN * timeout);
    udpBroadcast.udpBroadcastSend(ESPHTTPServer.getUpdPortTx(), ESPHTTPServer.udpJsonBroadcast());
  }
}
