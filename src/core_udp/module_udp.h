#ifndef _MODUDP_h
#define _MODUDP_h

#include "main.h"
#include "FSWebServerLib.h"

#ifdef ESP32
#include <AsyncUDP.h>
#else
#include <ESPAsyncUDP.h>
#endif

#ifdef DEBUG_UDP
#define DEBUGUDP(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGUDP(...)
#endif


const char Page_GeneralUdp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/udp.html">
Please Wait....Configuring.
)=====";




#define CONFIG_FILE_UDP     "/config_udp.json"
#define HTML_FILE_UDP       "/udp.html"
#define UDP_DATA_LENGHT_MAX 64 // bytes
#define UDP_DATA_MESSAGE_LEGHT 1024 // bytes


#define LOCALHOST "127.0.0.1"
#define UDP_PORT  40001
#define UDP_BROADCAST_PORT_DFLT     40000
#define UDP_BROADCAST_TIME_DFLT     5
#define UDP_BROADCAST_KEYWORD_DFLT  "Ave_Omnissiah"
#define UDP_BROADCAST_POWERON       true

typedef struct {
    uint16_t udpPortTx;
    uint16_t udpPortRx;
    uint16_t udpTimeOut;
    bool  udpPowerOn;
    String  keyword;
} strUdpConfig;

void    udpResponse(IPAddress _Ip);
void    udpBroadcastTimer();
void    udpBroadcastSimple();
void    processUdpListenPacketHandler(AsyncUDPPacket &packet) ;

class  UDPBROADCAST_CLASS   {
    public:
    
        UDPBROADCAST_CLASS (uint16_t portListen);
        AsyncUDP udp_listen;
        void	webInit(void);
        void    begin(uint16_t _port) ;
        void    udpBroadcastSend(uint16_t _port, String _str);
    
        void    udpStop();
        strUdpConfig    _udpConfig; // UDP configuration
        void            udpBroadcastTest                            (AsyncWebServerRequest *request);
        void        send_udp_configuration_values_html (AsyncWebServerRequest *request);
        void        get_udp_configuration_html         (AsyncWebServerRequest *request);
        uint16_t    getUpdPortTx()      ;
        uint16_t    getUpdPortRx()      ;
        uint16_t    getudpTimeOut()     ;
        String      getudpKeyword()     ;
        bool        getudpPowerOn() ;
        String      udpJsonGet()  ;  
        uint8_t     isStart();
        private:
        bool load_config_UDP();
        bool save_configUDP();
        void defaultConfigUDP();
        
        protected: 
        uint8_t     isStarted = false;
        uint16_t portRx = UDP_PORT;
        uint16_t portTx = UDP_PORT+1;
};
extern UDPBROADCAST_CLASS udpBroadcast;

#endif // _MODUDP_h

