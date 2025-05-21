#ifndef _UDPHELPER_h
#define _UDPHELPER_h

#include "main.h"

void    udpResponse(IPAddress respIp );
void    udpBroadcastTimer();

class  UDPBROADCAST_CLASS{
public:
    UDPBROADCAST_CLASS (uint16_t portListen);
    void    udpStart(uint16_t _port) ;
    void    udpBroadcastSend(uint16_t _port, String _str);
   
    void    udpStop();
protected: 
    bool     isStarted = false;
    uint16_t portRx = UDP_PORT;
    uint16_t portTx = UDP_PORT+1;
// private:

};
extern UDPBROADCAST_CLASS udpBroadcast;

#endif // _UDPHELPER_h