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
#define UDP_DATA_LENGHT_MAX 64      // bytes for receive
#define UDP_DATA_MESSAGE_LEN 1024    // bytes for send

#define UDP_PORT  40001
#define UDP_BROADCAST_PORT_DFLT     40000
#define UDP_BROADCAST_TIME_DFLT     5
#define UDP_BROADCAST_KEYWORD_DFLT  "Ave_Omnissiah"
#define UDP_BROADCAST_POWERON       true
#define UDP_BROADCAST_RESPONSE       true

typedef struct {
    String keyword;
    uint16_t udpPortTx;
    uint16_t udpPortRx;
    uint16_t udpTimeOut;
    bool  udpPowerOn;
    bool  udpResponse;
} strUdpConfig;

void udpBroadcastSimple(void);
void udpBroadcastTimer(void);

void processUdpListenPacket(AsyncUDPPacket &packet);
void udpResponseHandler(IPAddress ip);

class UDPBROADCAST_CLASS {
    public:
        UDPBROADCAST_CLASS(uint16_t portListen);
        
        void webInit(void);
        void begin();
        void udpStop();
        void udpBroadcastSend(uint16_t _port, String _str);
        void udpBroadcastTest(AsyncWebServerRequest *request);
        
        void send_udp_configuration_values_html(AsyncWebServerRequest *request);
        void get_udp_configuration_html(AsyncWebServerRequest *request);
        
        uint16_t getUpdPortTx();
        uint16_t getUpdPortRx();
        uint16_t getudpTimeOut();
        String getudpKeyword();
        bool udpPowerOnGet();
        bool udpResponseGet();
        String udpJsonGet();
        uint8_t isStart();
        
        strUdpConfig _udpConfig; // UDP configuration
        String getVersionStr();
        String getGeneratedTime();
        String getCommitDateStr();

    private:
        AsyncUDP _udp;
        IPAddress _responseIp;
        
        bool load_config_UDP();
        bool save_configUDP();
        void defaultConfigUDP();
        
        char _sendBuffer[UDP_DATA_MESSAGE_LEN];
        
        bool _isSending;
        unsigned long _lastSendTime;
        static const unsigned long SEND_TIMEOUT = 500; // 500ms
        
        uint8_t _isStarted;
        uint16_t _portRx;
        uint16_t _portTx;
        
        friend void udpBroadcastTimer();
        friend void processUdpListenPacket(AsyncUDPPacket &packet);
        friend void udpResponseHandler(IPAddress ip);
        friend void udpBroadcastSimple(void);
};

extern UDPBROADCAST_CLASS udpBroadcast;

#endif // _MODUDP_h