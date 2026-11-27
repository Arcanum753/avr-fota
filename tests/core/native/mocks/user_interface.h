#ifndef _MOCK_USER_INTERFACE_H
#define _MOCK_USER_INTERFACE_H

// Хост-мок SDK-заголовка ESP8266 для native-тестов (core_wifi: apClientsText).
// Реальные функции не вызываются: тесты работают в STA/скане.

#include <stdint.h>

struct ip_addr_mock {
    uint32_t addr;
};

struct station_info {
    struct station_info* next;
    uint8_t bssid[6];
    struct ip_addr_mock ip;
};

#ifndef STAILQ_NEXT
#define STAILQ_NEXT(elm, field) ((elm)->field)
#endif

static inline struct station_info* wifi_softap_get_station_info(void) { return 0; }
static inline void wifi_softap_free_station_info(void) {}

#endif // _MOCK_USER_INTERFACE_H
