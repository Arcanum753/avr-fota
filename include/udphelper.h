#ifndef _UDPHELPER_h
#define _UDPHELPER_h

#include "main.h"
#include "FSWebServerLib.h"

#define CONFIG_FILE_UDP             "/config_udp.json"
#define HTML_FILE_UDP             "/udp.html"

#define UDP_BROADCAST_PORT_DFLT 40000
#define UDP_BROADCAST_TIME_DFLT 5
#define UDP_BROADCAST_KEYWORD_DFLT "Ave_Omnissiah"

typedef struct {
    uint16_t udpPortTx;
    uint16_t udpPortRx;
    uint16_t udpTimeOut;
    String keyword;
} strUdpConfig;

void    udpResponse();
void    udpBroadcastTimer();

class  UDPBROADCAST_CLASS{
    public:
        UDPBROADCAST_CLASS (uint16_t portListen);
        void	udpInit(void);
        void    udpStart(uint16_t _port) ;
        void    udpBroadcastSend(uint16_t _port, String _str);
    
        void    udpStop();
        strUdpConfig    _udpConfig; // UDP configuration
        void udpTest                            (AsyncWebServerRequest *request);
        void send_udp_configuration_values_html (AsyncWebServerRequest *request);
        void get_udp_configuration_html         (AsyncWebServerRequest *request);
        uint16_t    getUpdPortTx()      ;
        uint16_t    getUpdPortRx()      ;
        uint16_t    getudpTimeOut()     ;
        String      getudpKeyword()     ;
        String      udpJsonBroadcast()  ;  
    private:
        bool load_config_UDP();
        bool save_configUDP();
        void defaultConfigUDP();
        
    protected: 
        bool     isStarted = false;
        uint16_t portRx = UDP_PORT;
        uint16_t portTx = UDP_PORT+1;
};
extern UDPBROADCAST_CLASS udpBroadcast;

#endif // _UDPHELPER_h