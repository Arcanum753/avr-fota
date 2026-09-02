#include "main.h"
#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#include <avr/pgmspace.h>
#endif

#include <DNSServer.h>

#include "core_web/FSWebServerLib.h"
#include "common/common.h"
#include "debug.h"

#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#include "core_ntp/core_ntp.h"

#include "core_led/core_led.h"
#include "core_wifi_version.h"

#ifdef MODULE_OTACLIENT
#include "module_otaclient/module_otaclient.h"
#endif

#include "core_sys/eertos.h"

CLASS_CORE_WIFI 	core_wifi(false);
DNSServer 		dnsServer;

// Пауза между повторными сканами, когда сеть не находится и AP выключена (сек)
#define WIFI_RESCAN_PAUSE_SEC		20
// Бюджет попытки подключения, если scanTime <= 0 (сек)
#define WIFI_CONNECT_BUDGET_SEC		20
// Защита от «зависшего» скана (скан не завершается) — принудительный рестарт (сек)
#define WIFI_SCAN_STUCK_SEC			60

// ============================================================
// Паттерны светодиодной индикации статуса Wi-Fi (кассета модуля)
// ============================================================

static const char patWifiScan[]     PROGMEM = "*.*.*.*.";
static const char patWifiDisc[]     PROGMEM = "*.........";
static const char patWifiAP[]       PROGMEM = "*.*.*......";
static const char patWifiConn[]     PROGMEM = "*.*..";
static const char patWifiErr[]      PROGMEM = "*.*.*";

CLASS_CORE_WIFI :: CLASS_CORE_WIFI (bool _in) {
	 dumb = _in;
 }

#if defined(ESP32)
void CLASS_CORE_WIFI::begin(fs::LittleFSFS* fs)
#endif
#if defined(ESP8266)
    void CLASS_CORE_WIFI::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
	if (!_fs) { _fs->begin();  }// If LittleFS is not started
	connectionTimout = 0;
	String hostName = ESPHTTPServer.getHostName();
	WiFi.hostname(hostName.c_str());
	// Отключаем энергосбережение WiFi - иначе при длительном
	// простое вкладки ESP уходит в modem-sleep и сбрасывается (~5 мин)
#if defined(ESP32)
	WiFi.setSleep(false);
#endif
#if defined(ESP8266)
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
	// Try to load configuration from file system// Load defaults if any error
	if (!load_configWifi(3)) { defaultConfigWifi(3); _apConfig.APenable = true; 	}
	if (!load_configWifi(2)) { defaultConfigWifi(2); _apConfig.APenable = true; 	}
	if (!load_configWifi(1)) { defaultConfigWifi(1); _apConfig.APenable = true; 	}
	if (!load_configWifi(0)) { defaultConfigWifi(0); _apConfig.APenable = true;		}
	if (!load_configWifiSys()) { defaultConfigWifiSys(); }
	scanTime = _wifiScanTime * MINUTES;
	DEBUGLOGWIFI("_strWifis[0] %s\r\n", _strWifi0);
	DEBUGLOGWIFI("_strWifis[1] %s\r\n", _strWifi1);
	DEBUGLOGWIFI("_strWifis[2] %s\r\n", _strWifi2);
	DEBUGLOGWIFI("_strWifis[3] %s\r\n", _strWifi3);

	if (AP_ENABLE_BUTTON >= 0) {
		// Set AP mode if AP button was pressed
		if (_apConfig.APenable) {	configureWifiAP();	}
		// Set WiFi config
		else {	configureWifi();	}
	}
	// Set WiFi config
	else {	configureWifi(); 	}
	// 1-секундный автомат Wi-Fi выполняется в контексте loop() через EERTOS
	// (SetTimerTask), а не из Ticker/esp_timer — WiFi API в контексте loop безопасен.
	SetTimerTask(&CLASS_CORE_WIFI::s_secondTick, 1000);

// Register wifi Event to control connection LED and wifi connection status
	#if defined(ESP32)
	onStationModeConnectedHandler 		= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiConnected();		},	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
	onStationModeDisconnectedHandler 	= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiDisconnected(info);		},	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
	onStationModeGotIPHandler 			= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiConnectedGotIP();	}, 	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
#endif
#if defined(ESP8266)
	onStationModeConnectedHandler 		= WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) 		{	this->onWiFiConnected(data);		});
	onStationModeDisconnectedHandler 	= WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) 	{	this->onWiFiDisconnected(data);		});
	onStationModeGotIPHandler 			= WiFi.onStationModeGotIP([this](WiFiEventStationModeGotIP data) 				{	this->onWiFiConnectedGotIP(data);	});
#endif

}

void CLASS_CORE_WIFI::begin(ModContext& ctx) {
	_fs = ctx.fs;
	begin(ctx.fs);
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_CORE_WIFI::web_Init () {
    ESPHTTPServer.on("/wifi.html", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        this->send_network_configuration_html(request);
    });

    ESPHTTPServer.on("/wifi/info", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        this->send_info_values_html(request);
    });

    // JSON-эндпоинты для слотов
    for (int i = 0; i < 4; i++) {
        String path = "/api/wifi/slot/" + String(i);
        
        // GET - получение данных слота в JSON
        ESPHTTPServer.on(path.c_str(), HTTP_GET, [this, i](AsyncWebServerRequest *request) {
            if (!ESPHTTPServer.checkAuth(request)) {
                return request->requestAuthentication();
            }
            this->send_slot_json(request, i);
        });
        
        // POST - сохранение данных слота (вызывает handle_slot_post)
        ESPHTTPServer.on(path.c_str(), HTTP_POST,
            [this, i](AsyncWebServerRequest *request) {
                this->handle_slot_post(request, i);
            },
            NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                this->handle_slot_upload(request, data, len, index, total);
            }
        );
    }

    ESPHTTPServer.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {
            return request->requestAuthentication();
        }
        this->send_scanwifi(request);
    });

    // Эндпоинт для запуска сканирования WiFi (только для страницы wifi.html)
    ESPHTTPServer.on("/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {
            return request->requestAuthentication();
        }
        this->send_scanwifi_trigger(request);
    });

    //captive
    ESPHTTPServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        core_wifi.notifyApClientActivity();
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        core_wifi.notifyApClientActivity();
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        core_wifi.notifyApClientActivity();
        request->send(200, "text/plain", "Microsoft NCSI");
    });

    ESPHTTPServer.on("/wifi/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

    ESPHTTPServer.on("/wifi/sysconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        send_wifi_sysconf_json(request);
    });

    ESPHTTPServer.on("/wifi/sysconf", HTTP_POST,
        [this](AsyncWebServerRequest *request) { handle_wifi_sysconf_post(request); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handle_slot_upload(request, data, len, index, total);
        }
    );
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_WIFI::send_info_values_html(AsyncWebServerRequest *request) {
	DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	String state = "N/A";
	String Networks = "";
	if (WiFi.status() == 0) {	state = "Idle";	}
	if (WiFi.status() == 1) {	state = "NO SSID AVAILBLE";}
	if (WiFi.status() == 2) {	state = "SCAN COMPLETED";}
	if (WiFi.status() == 3) {	state = "CONNECTED";}
	if (WiFi.status() == 4) {	state = "CONNECT FAILED";}
	if (WiFi.status() == 5) {	state = "CONNECTION LOST";}
	if (WiFi.status() == 6) {	state = "DISCONNECTED";}

	String values = "";
	values += "connectionstate|" + state + "|div\n";
	
	values += "x_ssid|" 	+ (String)WiFi.SSID() + "|div\n";
	values += "x_ip|" 		+ (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + "|div\n";
	values += "x_gateway|" 	+ (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + "|div\n";
	values += "x_netmask|" 	+ (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + "|div\n";
	values += "x_mac|" 		+ getMacAddress() + "|div\n";
	values += "x_dns|" 		+ (String)WiFi.dnsIP()[0] + "." + (String)WiFi.dnsIP()[1] + "." + (String)WiFi.dnsIP()[2] + "." + (String)WiFi.dnsIP()[3] + "|div\n";

	request->send(200, "text/plain", values);
	state = "";
	values = "";
	Networks = "";
}

void CLASS_CORE_WIFI::send_scanwifi(AsyncWebServerRequest *request) {
    String json = buildNetworksJson();
    request->send(200, "text/json", json);
}

void CLASS_CORE_WIFI::send_scanwifi_trigger(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI(__FUNCTION__); DEBUGLOGWIFI("\r\n");
    int scanStatus = WiFi.scanComplete();
    String json = "{";
    
    if (scanStatus == WIFI_SCAN_RUNNING) {
        json += "\"status\":\"already_running\"";
        DEBUGLOGWIFI("Scan already running\n");
    } else {
        WiFi.scanNetworks(true);
        json += "\"status\":\"started\"";
        DEBUGLOGWIFI("Scan triggered\n");
    }
    
    json += "}";
    request->send(200, "application/json", json);
}

void CLASS_CORE_WIFI::send_network_configuration_html(AsyncWebServerRequest *request) {
	// DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		//bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOGWIFI("Arg %d: %s\r\n", i, request->arg(i).c_str());
			if (request->argName(i) == "ssid") 		{ _wifiConfig.ssid = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "password")	{ _wifiConfig.password = urldecode(request->arg(i)); continue; }
			if (request->argName(i) == "ip_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_0") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_1") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_2") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_3") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dhcp")  { _wifiConfig.dhcp = true; continue; }
			if (request->argName(i) == "wificonf")  { if (checkRange(request->arg(i))) 	_saveIn = request->arg(i).toInt(); continue; }
		}
		request->send_P(200, "text/html", Page_ConfigRefresh);

		if (_saveIn == 0) {save_configWifi(0);}
		if (_saveIn == 1) {save_configWifi(1);}
		if (_saveIn == 2) {save_configWifi(2);}
		if (_saveIn == 3) {save_configWifi(3);}

	}
	else {
		DEBUGLOGWIFI("%s\r\n", request->url().c_str());
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}
// wifi.html ^^^

void CLASS_CORE_WIFI::send_slot_json(AsyncWebServerRequest *request, int slot) {
    DEBUGLOGWIFI("Sending slot %d data as JSON\n", slot);
    load_configWifi(slot);

    String response = core_json.jsonBuildSlotConfig(
        _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
        _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns
    );
    request->send(200, "application/json", response);
}
void CLASS_CORE_WIFI::handle_slot_post(AsyncWebServerRequest *request, int slot) {
    if (!ESPHTTPServer.checkAuth(request)) {
        // Освобождаем память перед возвратом
        if (request->_tempObject) {
            free(request->_tempObject);
            request->_tempObject = NULL;
        }
        return request->requestAuthentication();
    }
    
    if (request->_tempObject != NULL) {
        String body = String((char*)request->_tempObject);
        DEBUGLOGWIFI("Received body for slot %d: %s\n", slot, body.c_str());
        
        String ssid, password;
        bool dhcp = false;
        IPAddress ip, netmask, gateway, dns;
        
        this->load_configWifi(slot);
        ssid = this->_wifiConfig.ssid;
        password = this->_wifiConfig.password;
        dhcp = this->_wifiConfig.dhcp;
        ip = this->_wifiConfig.ip;
        netmask = this->_wifiConfig.netmask;
        gateway = this->_wifiConfig.gateway;
        dns = this->_wifiConfig.dns;
        
        int parsed = core_json.jsonParseSlotConfig(body, ssid, password, dhcp,
                                                        ip, netmask, gateway, dns);
        
        // Освобождаем память
        free(request->_tempObject);
        request->_tempObject = NULL;
        
        if (parsed == 0) {
            DEBUGLOGWIFI("JSON parse error\n");
            request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
            return;
        }
        
        this->_wifiConfig.ssid = ssid;
        this->_wifiConfig.password = password;
        this->_wifiConfig.dhcp = dhcp;
        this->_wifiConfig.ip = ip;
        this->_wifiConfig.netmask = netmask;
        this->_wifiConfig.gateway = gateway;
        this->_wifiConfig.dns = dns;
        
        bool saveResult = this->save_configWifi(slot);
        
        if (saveResult) {
            request->send(200, "application/json", "{\"success\":true}");
            DEBUGLOGWIFI("Saved slot %d ok.\n", slot);
        } else {
            request->send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            DEBUGLOGWIFI("Saved slot %d failed.\n", slot);
        }
    } else {
        DEBUGLOGWIFI("No body data for slot %d\n", slot);
        request->send(400, "application/json", "{\"success\":false,\"error\":\"No data\"}");
    }
}
// Обработчик для загрузки тела запроса (добавьте эту функцию)
void CLASS_CORE_WIFI::handle_slot_upload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!request->_tempObject) {
        char *buff = (char*)malloc(total + 1);
        request->_tempObject = buff;
    }
    
    char *buff = (char*)request->_tempObject;
    memcpy(buff + index, data, len);
    
    if (index + len == total) { buff[total] = '\0'; }
}

void CLASS_CORE_WIFI::send_wifi_sysconf_json(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("send_wifi_sysconf_json\n");
    String values = "";
    values += "scantime_hours|" + String(_wifiScanTime / 60) + "|input\n";
    values += "scantime_mins|" + String(_wifiScanTime % 60) + "|input\n";
    values += "aptime|" + String(_wifiAPLifeTime) + "|input\n";
    request->send(200, "text/plain", values);
}

void CLASS_CORE_WIFI::handle_wifi_sysconf_post(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("handle_wifi_sysconf_post\n");
    if (!ESPHTTPServer.checkAuth(request)) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = NULL; }
        return request->requestAuthentication();
    }

    if (!request->_tempObject) { request->send(400, "application/json", "{\"success\":false}"); return; }

    String body = String((char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = NULL;

    int32_t scantimeVal = 0, aptimeVal = 0;
    if (!core_json.jsonParseInt(body, "scantime", scantimeVal)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
        return;
    }
    if (scantimeVal < 0) scantimeVal = 0;
    if (scantimeVal > 720) scantimeVal = 720;
    _wifiScanTime = (uint16_t)scantimeVal;

    core_json.jsonParseInt(body, "aptime", aptimeVal);
    if (aptimeVal < 0) aptimeVal = 0;
    if (aptimeVal > 60) aptimeVal = 60;
    _wifiAPLifeTime = (uint16_t)aptimeVal;

    if (save_configWifiSys()) {
        scanTime = _wifiScanTime * MINUTES;
        request->send(200, "application/json", "{\"success\":true}");
        DEBUGLOGWIFI("WiFi sys config saved: scantime=%d, aptime=%d\n", _wifiScanTime, _wifiAPLifeTime);
    } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
    }
}

// ============================================================
// Конфиг
// ============================================================

bool CLASS_CORE_WIFI::load_configWifi(int _in) {
	if (_in < 0){ return false; }
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	if (!core_json.jsonFileLoadSlot(filename, _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
	                                   _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns)) {
	    return false;
	}
	if (_in == 0)  sprintf(_strWifi0, "%s", _wifiConfig.ssid.c_str());
	if (_in == 1)  sprintf(_strWifi1, "%s", _wifiConfig.ssid.c_str());
	if (_in == 2)  sprintf(_strWifi2, "%s", _wifiConfig.ssid.c_str());
	if (_in == 3)  sprintf(_strWifi3, "%s", _wifiConfig.ssid.c_str());

	return true;
}

bool CLASS_CORE_WIFI::save_configWifi(int _in) {
	//flag_config = false;
	DEBUGLOGWIFI("Save config\r\n");
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	return core_json.jsonFileSaveSlot(filename, _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
	                                      _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns);
}

void CLASS_CORE_WIFI::defaultConfigWifi(int _in) {
	// DEFAULT CONFIG
	_wifiConfig.ssid 		= "YOUR_DEFAULT_WIFI_SSID";
	_wifiConfig.password 	= "YOUR_DEFAULT_WIFI_PASSWD";
	_wifiConfig.dhcp 		= 1;
	_wifiConfig.ip 			= IPAddress(192, 168, 1, 4);
	_wifiConfig.netmask 	= IPAddress(255, 255, 255, 0);
	_wifiConfig.gateway 	= IPAddress(192, 168, 1, 1);
	_wifiConfig.dns 		= IPAddress(192, 168, 1, 1);
	
	save_configWifi(_in);
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}

bool CLASS_CORE_WIFI::load_configWifiSys() {
    DEBUGLOGWIFI("Loading WiFi sys config\n");
    JsonDocument doc;
    if (!core_json.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc)) return false;
    _wifiScanTime = doc["scantime"].as<uint16_t>();
    _wifiAPLifeTime = doc["aptime"].as<uint16_t>();
    return true;
}

bool CLASS_CORE_WIFI::save_configWifiSys() {
    DEBUGLOGWIFI("Saving WiFi sys config\n");
    JsonDocument doc;
    core_json.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc);
    doc["scantime"] = _wifiScanTime;
    doc["aptime"] = _wifiAPLifeTime;
    return core_json.jsonFileSaveDoc(WIFI_CONFIG_SYS, doc);
}

void CLASS_CORE_WIFI::defaultConfigWifiSys() {
    DEBUGLOGWIFI("defaultConfigWifiSys\n");
    _wifiScanTime = 1;
    _wifiAPLifeTime = 10;
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_CORE_WIFI::getVersionStr(){
    return String(CORE_WIFI_VERSION);
}

String CLASS_CORE_WIFI::getGeneratedTime(){
    return String(CORE_WIFI_GENERATED_TIME);
}

String CLASS_CORE_WIFI::getCommitDateStr(){
    return String(CORE_WIFI_COMMIT_DATE_STR);
}

void CLASS_CORE_WIFI::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("%s\n\r", __FUNCTION__);
    String values = "";
    values += "wifiversion|"     + getVersionStr()    + "|div\n";
    values += "wifigentime|"     + getGeneratedTime() + "|div\n";
    values += "wifigendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

void CLASS_CORE_WIFI::s_secondTick() {
	SetTimerTask(&CLASS_CORE_WIFI::s_secondTick, 1000);
	core_wifi.secondTick();
}

void CLASS_CORE_WIFI::secondTick() {
	_stateSeconds++;

	if (_suppressDisc > 0) { _suppressDisc--; }

	// Периодический сброс счётчиков неудачных попыток раз в 60 секунд —
	// независимо от состояния (раньше привязка к connectionTimout не срабатывала в AP)
	if (_stateSeconds % 60 == 0) {
		bool anyBlocked = false;
		for (int i = 0; i < 4; i++) {
			if (_wifiFailCount[i] >= MAX_WIFI_FAIL_COUNT) { anyBlocked = true; break; }
		}
		if (anyBlocked) {
			DEBUGLOGWIFI("Periodic reset of wifi fail counters\n");
			resetWifiFailCounters();
		}
	}

	if (wifiStatus == FS_STAT_APMODE) {
		//DNS captive
		dnsServer.processNextRequest();
		apTick();
		return;
	}

	// Отложенный вход в AP из WiFi-события выполняем в контексте loop
	if (_enterApPending) {
		_enterApPending = false;
		enterApWait();
		return;
	}

	if (wifiStatus == FS_STAT_CONNECTED) {
		ledMacrosWifiConnected();
		return;
	}
	if (wifiStatus == FS_STAT_CONNECTING) {
		staTick();
		return;
	}
}

// AP «живёт» максимум _wifiAPLifeTime минут без активности:
// клиент не подключился, либо висит без трафика. Затем — скан сети.
void CLASS_CORE_WIFI::apTick() {
	if (_wifiAPLifeTime == 0) {
		leaveApToScan();
		return;
	}

	// Любая HTTP-активность клиента (notifyApClientActivity из сервера) продлевает AP
	if (_apClientActivity) {
		_apClientActivity = false;
		_apUptime = 0;
		ledMacrosWifiAP();
		return;
	}

	if ((uint32_t)++_apUptime >= (uint32_t)_wifiAPLifeTime * 60) {
		DEBUGLOGWIFI("AP idle %lu sec without activity. Leaving AP to scan.\r\n", (unsigned long)_wifiAPLifeTime * 60);
		leaveApToScan();
		return;
	}
	ledMacrosWifiAP();
}

void CLASS_CORE_WIFI::leaveApToScan() {
	DEBUGLOGWIFI("AP -> STA scan\r\n");
	dnsServer.stop();
	_suppressDisc = 3;   // события от переключения режимов игнорируем
	_ignoreDisconnect = true;
	WiFi.softAPdisconnect(true);
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	_nextStaScanAt = _stateSeconds;
	WiFi.scanNetworks(true);
	ledMacrosWifiScan();
}

// Вход в «AP в ожидании клиента». При _wifiAPLifeTime == 0 AP не включается —
// остаёмся в STA и периодически пересканируем сеть.
void CLASS_CORE_WIFI::enterApWait() {
	if (wifiStatus == FS_STAT_APMODE) {
		_apUptime = 0;
		return;
	}
	if (_wifiAPLifeTime == 0) {
		wifiStatus = FS_STAT_CONNECTING;
		WifiScan = WF_STAT_SCANING;
		connectionTimout = 0;
		_apUptime = 0;
		_apClientActivity = false;
		_nextStaScanAt = _stateSeconds;
		return;
	}
	configureWifiAP();
}

void CLASS_CORE_WIFI::rescanSoon() {
	_nextStaScanAt = _stateSeconds; // ближайший тик начнёт скан
}

bool CLASS_CORE_WIFI::anySlotFree() {
	for (int i = 0; i < 4; i++) {
		if (_wifiFailCount[i] < MAX_WIFI_FAIL_COUNT) { return true; }
	}
	return false;
}

// STA: скан сети из конфигов / попытка подключения
void CLASS_CORE_WIFI::staTick() {
	// Идёт попытка подключения (WiFi.begin вызван) — контролируем бюджет
	if (WifiScan == WF_SCAN_NO_NEED) {
		uint32_t budget = (scanTime > 0) ? (uint32_t)scanTime : WIFI_CONNECT_BUDGET_SEC;
		if ((uint32_t)++connectionTimout >= budget) {
			DEBUGLOGWIFI("Connect budget expired. Back to AP wait.\r\n");
			enterApWait();
		}
		return;
	}

	// Пауза между повторными сканами (STA-режим без AP, сеть не находится)
	if (_stateSeconds < _nextStaScanAt) {
		ledMacrosWifiScan();
		return;
	}

	int st = WiFi.scanComplete();
	if (st == WIFI_SCAN_RUNNING) {
		// Защита от «зависшего» скана: если скан не завершается дольше порога —
		// перезапускаем через AP-ожидание или повторный скан
		if ((uint32_t)++connectionTimout >= WIFI_SCAN_STUCK_SEC) {
			DEBUGLOGWIFI("Scan stuck %lu sec. Restarting.\r\n", (unsigned long)WIFI_SCAN_STUCK_SEC);
			connectionTimout = 0;
			WiFi.scanDelete();
			if (_wifiAPLifeTime > 0) {
				enterApWait();
			} else {
				WiFi.mode(WIFI_STA);
				_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
				ledMacrosWifiScan();
			}
		} else {
			ledMacrosWifiScan();
		}
		return;
	}
	if (st == WIFI_SCAN_FAILED) {
		connectionTimout = 0;
		WiFi.scanNetworks(true);	// перезапуск скана
		ledMacrosWifiScan();
		return;
	}
	if (st < 0) {
		ledMacrosWifiScan();
		return;
	}

	// Скан завершён
	connectionTimout = 0;
	int slot = scanWifi();
	WiFi.scanDelete();
	if (slot < 0) {
		// Сети из конфигов нет (или все SSID заблокированы счётчиками неудач)
		if (_wifiAPLifeTime > 0) {
			enterApWait();
		} else {
			_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
			ledMacrosWifiScan();
		}
		return;
	}

	// Найдена сеть из конфигов — подключаемся
	load_configWifi(slot);
	WifiScan = WF_SCAN_NO_NEED;
	connectionTimout = 0;
	DEBUGLOGWIFI("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
	WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	ledMacrosWifiConnecting();
}

// ============================================================
// Светодиодная индикация статуса Wi-Fi
// ============================================================

void ledMacrosWifiScan()			{	ledSetState(LED_PRIO_WIFI, patWifiScan, 10);  }
void ledMacrosWifiDisconnect()		{	ledSetState(LED_PRIO_WIFI, patWifiDisc, 3);  }
void ledMacrosWifiAP()				{	ledSetState(LED_PRIO_WIFI, patWifiAP, -1); }
void ledMacrosWifiConnecting()		{	ledSetState(LED_PRIO_WIFI, patWifiConn, 2); }
void ledMacrosWifiError()			{	ledSetState(LED_PRIO_WIFI, patWifiErr, 5); }
void ledMacrosWifiConnected()		{	ledSetSteady(true); ledClearState(LED_PRIO_WIFI); }

void CLASS_CORE_WIFI::startDNSCaptive() {
    // Перехватываем все DNS запросы и направляем на IP точки доступа
    dnsServer.start(53, "*", WiFi.softAPIP());
    DEBUGLOGWIFI("DNS captive portal started on port 53\n");
}

void CLASS_CORE_WIFI::configureWifiAP() {
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	core_ntp.ntpOnDisconected();
#if defined(MODULE_UDP)
		module_udp.stop();	// always stop!
#endif
	String APname = ESPHTTPServer.getHostName();
	_suppressDisc = 3;   // события от собственного отключения STA игнорируем
	_ignoreDisconnect = true;
	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_APMODE;
	if (ESPHTTPServer._httpAuth.auth) {
		WiFi.softAP(APname, ESPHTTPServer._httpAuth.wwwPassword);
		DEBUGLOGWIFI("AP Pass enabled: %s \r\n", ESPHTTPServer._httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOGWIFI("AP Pass disabled \r\n");
	}
	startDNSCaptive();
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 5, 250);	}
	DEBUGLOGWIFI("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	ledSetSteady(false);
	ledMacrosWifiAP();	// вход в AP-режим
}

int CLASS_CORE_WIFI::scanWifi() {
	int _scanNum = -1;

	// Выбор SSID из завершённого скана в порядке приоритета (сначала слот 3, потом 2, 1, 0).
	// Пропускаем SSID, у которых превышен лимит неудачных попыток.
	int nets = WiFi.scanComplete();
	if (nets <= 0) { return -1; }

	for (int i = 0; i < nets && _scanNum < 0; ++i) {
		if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[3] < MAX_WIFI_FAIL_COUNT){ _scanNum = 3; }
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[2] < MAX_WIFI_FAIL_COUNT){ _scanNum = 2; }
		}
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[1] < MAX_WIFI_FAIL_COUNT){ _scanNum = 1; }
		}
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[0] < MAX_WIFI_FAIL_COUNT){ _scanNum = 0; }
		}
	}
	return _scanNum;
}

void CLASS_CORE_WIFI::configureWifi() { // вход в STA-режим: скан сети / подключение
	if (wifiStatus == FS_STAT_APMODE) {return;}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	//disconnect required here
	//improves reconnect reliability
	_suppressDisc = 3;   // события от собственного disconnect() игнорируем
	_ignoreDisconnect = true;
	if (WiFi.isConnected()) {	WiFi.disconnect(); 	}
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	ledSetSteady(false);	// выход из steady-on при подключении
	_apUptime = 0;
	_apClientActivity = false;
	_nextStaScanAt = _stateSeconds;
	WiFi.scanNetworks(true);
	ledMacrosWifiScan();
}

#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiConnected()
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiConnected(WiFiEventStationModeConnected data)
#endif
{
	DEBUGLOGWIFI("WiFi Connected: Waiting for DHCP\n\r");
	if (CONNECTION_LED >= 0) {espLedOn(); 	}	// Turn LED on
	wifiDisconnectedSince = 0;
	// Сбрасываем счётчик неудачных попыток при успешном подключении
	resetWifiFailCounters();

}

// Do functions when we get IP.
//means we get nor,al connection 	
#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiConnectedGotIP() {
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
#endif
	ledMacrosWifiConnected();	// выход из всех wifi-морганий: steady-on

	DEBUGLOGWIFI("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DEBUGLOGWIFI("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUGLOGWIFI("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	wifiDisconnectedSince = 0;
	connectionTimout = 0;
	wifiStatus = FS_STAT_CONNECTED;
	_enterApPending = false;
	_suppressDisc = 0;
#if defined(MODULE_UDP)
//udp start to listen
	module_udp.begin();
	//udp broadcast - we are online!
    if (module_udp.powerOnGet() == true ) {  broadcastSimple(); }
#endif
	core_ntp.ntpOnConnected();

#ifdef MODULE_OTACLIENT
	// Trigger OTA client check on WiFi connect (if powerOn enabled)
	module_otaclient.onWiFiConnect();
#endif

}

#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiDisconnected(WiFiEventInfo_t info) {
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#endif

	// Собственные отключения (переключение режимов, restart) не обрабатываем
	if (_ignoreDisconnect || _suppressDisc > 0 || wifiStatus == FS_STAT_RESET) { return; }

#if defined(MODULE_UDP)
	module_udp.stop();	// always stop!
#endif
	core_ntp.ntpOnDisconected();
	ledSetSteady(false);	// выход из steady-on при отключении

	uint8_t reason = 0;
#if defined(ESP8266)
	reason = data.reason;
#endif
#if defined(ESP32)
	reason = info.wifi_sta_disconnected.reason;
#endif
	DEBUGLOGWIFI("STA disconnected, reason: %u\r\n", (unsigned)reason);

	// «Неверный пароль» определяем по точной причине из события, а не по WiFi.status().
	// NO_AP_FOUND (пропал роутер) неверным паролем НЕ считается.
	bool authFail = false;
#if defined(ESP8266)
	authFail = (reason == WIFI_DISCONNECT_REASON_AUTH_FAIL);
#endif
#if defined(ESP32)
	authFail = (reason == WIFI_REASON_AUTH_FAIL);
#endif

	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUGLOGWIFI("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));

	wifiStatus = FS_STAT_CONNECTING;
	connectionTimout = 0;

	if (authFail) {
		DEBUGLOGWIFI("Auth fail (wrong password?): %s\r\n", _wifiConfig.ssid.c_str());
		wifiSsidSetPSWDwrong(_wifiConfig.ssid);
		ledMacrosWifiDisconnect();
		if (anySlotFree()) {
			// Остались незаблокированные слоты — пересканируем и пробуем следующий
			WifiScan = WF_STAT_SCANING;
			rescanSoon();
		} else {
			// Все сети заблокированы — в AP, чтобы пользователь исправил пароль
			_enterApPending = true;
		}
		return;
	}

	// Роутер пропал или попытка подключения не удалась.
	// Логика: сначала AP ждёт клиента (если включена), затем скан сети.
	WifiScan = WF_STAT_SCANING;
	if (_wifiAPLifeTime > 0) {
		_enterApPending = true;
	} else {
		rescanSoon();
	}
}

void CLASS_CORE_WIFI::wifiSsidSetPSWDwrong(String _str) {
	DEBUGLOGWIFI("wifi ssid wrong password: %s \n", _str.c_str());
	// Вместо безвозвратного удаления SSID — инкрементируем счётчик неудач
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	_wifiFailCount[3]++; }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	_wifiFailCount[2]++; }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	_wifiFailCount[1]++; }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	_wifiFailCount[0]++; }
}

void CLASS_CORE_WIFI::resetWifiFailCounters() {
	DEBUGLOGWIFI("resetWifiFailCounters\n");
	for (int i = 0; i < 4; i++) {
		_wifiFailCount[i] = 0;
	}
}

String CLASS_CORE_WIFI::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}

String CLASS_CORE_WIFI::buildNetworksJson() {
    String json = "[";
    int n = WiFi.scanComplete();
    
    // НЕ запускаем WiFi.scanNetworks() из HTTP-контекста!
    // Сканирование запускается ТОЛЬКО через /wifi/scan эндпоинт
    // Если сканирование не завершено или не запущено — возвращаем пустой массив
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (i) json += ",";
            json += "{";
            json += "\"rssi\":" + String(WiFi.RSSI(i));
            json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
            json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
            json += ",\"channel\":" + String(WiFi.channel(i));
            json += ",\"secure\":" + String(WiFi.encryptionType(i));
#ifdef ESP8266
            json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
#endif
#ifdef ESP32
            // TODO: Add hidden network support for ESP32 if needed
#endif
            json += "}";
        }
        WiFi.scanDelete();
    }
    json += "]";
    return json;
}
