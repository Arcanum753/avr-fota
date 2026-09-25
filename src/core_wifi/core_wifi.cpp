#include "main.h"
#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif

#include "core_web/FSWebServerLib.h"
#include "common/common.h"
#include "debug.h"

#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"
#include "core_sys/core_sys.h"
#include "core_state/core_state.h"
#include "core_wifi_version.h"

#include "core_sys/eertos.h"

CLASS_CORE_WIFI 	core_wifi;

// Имена ENUM-значений для каталога шины и CVT-вывода (числовые индексы — в core_wifi_types.h).
static const char* const kWifiModeNames[]   = { "auto", "macro" };
static const char* const kWifiTargetNames[] = { "auto", "ap", "sta" };

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
	String hostName = core_sys.getHostName();
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
	DEBUG_CORE_WIFI("_strWifis[0] %s\r\n", _strWifi0);
	DEBUG_CORE_WIFI("_strWifis[1] %s\r\n", _strWifi1);
	DEBUG_CORE_WIFI("_strWifis[2] %s\r\n", _strWifi2);
	DEBUG_CORE_WIFI("_strWifis[3] %s\r\n", _strWifi3);

	if (AP_ENABLE_BUTTON >= 0) {
		// Кнопка AP читается после загрузки конфигов, чтобы конфиг её не перезаписал.
		// Нажатие принудительно включает AP, но не отключает AP при отсутствии конфигов.
		pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP);
		if (!digitalRead(AP_ENABLE_BUTTON)) {	_apConfig.APenable = true;	}
		DEBUG_CORE_WIFI("AP Enable = %d\n", _apConfig.APenable);
	}
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

// AP-события (вариант A / Q14): счётчик клиентов AP для wifi.ap_clients/ap_busy.
#if defined(ESP32)
	_onApStationConnectedHandler    = WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) { this->onApStationConnected();    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);
	_onApStationDisconnectedHandler = WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) { this->onApStationDisconnected(); }, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
#endif
#if defined(ESP8266)
	_onApStationConnectedHandler    = WiFi.onSoftAPModeStationConnected([this](WiFiEventSoftAPModeStationConnected data)          { this->onApStationConnected();    });
	_onApStationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected([this](WiFiEventSoftAPModeStationDisconnected data)    { this->onApStationDisconnected(); });
#endif

}

void CLASS_CORE_WIFI::begin(ModContext& ctx) {
	_fs = ctx.fs;
	begin(ctx.fs);

	// Сигналы конфига и runtime-дефолтов — каталог шины сразу показывает корректные значения.
	core_state.signal("wifi.mode", BusValue::en((int32_t)_busMode));
	core_state.signal("wifi.scan_retries", BusValue::i32((int32_t)_scanRetries));
	core_state.signal("wifi.target", BusValue::en(WIFI_TARGET_AUTO));
	core_state.signal("wifi.slot", BusValue::str(""));
	core_state.signal("wifi.ap_hold_min", BusValue::i32(0));
	core_state.signal("wifi.ap_mode", BusValue::bo(false));
	core_state.signal("wifi.ap_clients", BusValue::i32(0));
	core_state.signal("wifi.ap_busy", BusValue::bo(false));
	core_state.signal("wifi.slot_name", BusValue::str(""));
}

// ============================================================
// register_resources()
// ============================================================
void CLASS_CORE_WIFI::register_resources() {
    DEBUG_CORE_WIFI("%s\r\n", __FUNCTION__);

    // Состояния
    core_state.regState("connected",    BusValue::BOOL, "STA connected", false);
    core_state.regState("rssi",         BusValue::I32,  "Wi-Fi RSSI (dBm)", false);
    core_state.regState("ip",           BusValue::STR,  "STA IP address", false);
    core_state.regState("slot_name",    BusValue::STR,  "SSID текущего подключения", false);
    core_state.regState("ap_mode",      BusValue::BOOL, "AP поднят", false);
    core_state.regState("ap_clients",   BusValue::I32,  "клиентов AP", false);
    core_state.regState("ap_busy",      BusValue::BOOL, "AP занят (клиенты > 0)", false);

    // Режим и цель — ENUM-состояния
    core_state.regEnum("mode",   2, kWifiModeNames,   "режим модуля (auto/macro)");
    core_state.regEnum("target", 3, kWifiTargetNames, "целевое состояние (auto/ap/sta)");
    core_state.regState("slot",        BusValue::STR,  "целевой слот/SSID (\"\" = авто)", true);
    core_state.regState("ap_hold_min", BusValue::I32,  "минуты удержания AP (0 = бесконечно)", true);
    core_state.regState("scan_retries",BusValue::I32,  "сканов подряд без результата (5..50)", true);

    // События
    core_state.regEvent("just_connected",     "Wi-Fi just connected");
    core_state.regEvent("just_disconnected",  "Wi-Fi just disconnected");
    core_state.regEvent("ap_client_joined",   "клиент AP подключился");
    core_state.regEvent("ap_client_left",     "клиент AP отключился");
    core_state.regEvent("target_reached",     "автомат достиг цели");
    core_state.regEvent("sta_pending",        "цель STA зафиксирована, ждём освобождения AP");
    core_state.regEvent("sta_applied",        "цель STA применена (переход в STA выполнен)");

    // Функции (sync). mode/save не гейтятся, остальные — через wifiBusAllowed().
    core_state.regFunc("mode",              "->", "сменить режим (всегда доступно)",          CLASS_CORE_WIFI::s_cbMode, this);
    core_state.regFunc("save",              "->", "сохранить /config_wifi.json",              CLASS_CORE_WIFI::s_cbSave, this);
    core_state.regFunc("set_slot",          "s",  "задать слот/SSID (macro)",                 CLASS_CORE_WIFI::s_cbSetSlot, this);
    core_state.regFunc("force_ap",          "->", "в AP безопасно (macro)",                   CLASS_CORE_WIFI::s_cbForceAp, this);
    core_state.regFunc("force_ap_kick",     "->", "в AP, выгнав клиентов (macro)",            CLASS_CORE_WIFI::s_cbForceApKick, this);
    core_state.regFunc("force_connect",     "->", "подключиться к wifi.slot (macro)",         CLASS_CORE_WIFI::s_cbForceConnect, this);
    core_state.regFunc("force_connect_kick","->", "то же, выгнав клиентов AP (macro)",        CLASS_CORE_WIFI::s_cbForceConnectKick, this);
    core_state.regFunc("force_scan",        "n",  "серия из n сканов, лучшая сеть (macro)",   CLASS_CORE_WIFI::s_cbForceScan, this);
    core_state.regFunc("force_disconnect",  "->", "отключить STA (macro)",                    CLASS_CORE_WIFI::s_cbForceDisconnect, this);

    // Коды возврата (обязательно, для UI каталога и диагностики).
    core_state.regFuncCode("wifi.mode", BUS_ERR_BAD_VALUE, "значение вне диапазона (0/1)");
    core_state.regFuncCode("wifi.set_slot", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_ap", BUS_ERR_BUSY, "AP занят клиентами (нужен *_kick)");
    core_state.regFuncCode("wifi.force_ap", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_ap_kick", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_connect", BUS_ERR_BUSY, "AP занят клиентами (нужен *_kick)");
    core_state.regFuncCode("wifi.force_connect", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_connect", BUS_ERR_NOT_FOUND, "SSID не найден в слотах");
    core_state.regFuncCode("wifi.force_connect", BUS_ERR_NOT_READY, "слот найден, но не заполнен");
    core_state.regFuncCode("wifi.force_connect_kick", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_connect_kick", BUS_ERR_NOT_FOUND, "SSID не найден в слотах");
    core_state.regFuncCode("wifi.force_connect_kick", BUS_ERR_NOT_READY, "слот найден, но не заполнен");
    core_state.regFuncCode("wifi.force_scan", BUS_ERR_DENIED, "требуется режим macro");
    core_state.regFuncCode("wifi.force_scan", BUS_ERR_BAD_VALUE, "значение вне диапазона (5..50)");
    core_state.regFuncCode("wifi.force_disconnect", BUS_ERR_DENIED, "требуется режим macro");
}

// ============================================================
// Trampoline'ы BusCb (разрешено вызывать из контекста loop/core_state)
// ============================================================

int CLASS_CORE_WIFI::s_cbMode(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (argc < 1) return BUS_ERR_BAD_VALUE;
    int m = (int)argv[0].i;
    if (m != WIFI_MODE_AUTO && m != WIFI_MODE_MACRO) return BUS_ERR_BAD_VALUE;
    self->setWifiMode((uint8_t)m);
    return BUS_OK;
}

int CLASS_CORE_WIFI::s_cbSave(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    self->saveNow();
    return BUS_OK;
}

int CLASS_CORE_WIFI::s_cbSetSlot(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    if (argc < 1) return BUS_ERR_BAD_ARGC;
    self->setSlot(argv[0].s);
    return BUS_OK;
}

int CLASS_CORE_WIFI::s_cbForceAp(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    return self->forceAp();
}

int CLASS_CORE_WIFI::s_cbForceApKick(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    return self->forceApKick();
}

int CLASS_CORE_WIFI::s_cbForceConnect(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    return self->forceConnect();
}

int CLASS_CORE_WIFI::s_cbForceConnectKick(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    return self->forceConnectKick();
}

int CLASS_CORE_WIFI::s_cbForceScan(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    if (argc < 1) return BUS_ERR_BAD_ARGC;
    return self->forceScan((int)argv[0].i);
}

int CLASS_CORE_WIFI::s_cbForceDisconnect(void* user, int argc, const BusValue* argv, BusValue& result) {
    CLASS_CORE_WIFI* self = (CLASS_CORE_WIFI*)user;
    if (!self->wifiBusAllowed()) return BUS_ERR_DENIED;
    return self->forceDisconnect();
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
	DEBUG_CORE_WIFI(__FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
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

	// Состояние ресурсной шины (ENUM-значения — строкой; число остаётся в каталоге core_state)
	values += "mode|" 		+ String(kWifiModeNames[_busMode]) + "|div\n";
	values += "target|" 	+ String(kWifiTargetNames[_target]) + "|div\n";
	values += "ap_hold_min|" + String((uint32_t)_apHoldMin) + "|div\n";
	values += "scan_retries|" + String((int)_scanRetries) + "|div\n";
	values += "ap_mode|" 	+ String((WiFi.getMode() == WIFI_AP) ? 1 : 0) + "|div\n";
	values += "ap_clients|" + String((int)_apClientCount) + "|div\n";
	values += "ap_busy|" 	+ String((_apClientCount > 0) ? 1 : 0) + "|div\n";
	values += "slot_name|" 	+ (String)WiFi.SSID() + "|div\n";

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
    DEBUG_CORE_WIFI(__FUNCTION__); DEBUG_CORE_WIFI("\r\n");
    int scanStatus = WiFi.scanComplete();
    String json = "{";
    
    if (scanStatus == WIFI_SCAN_RUNNING) {
        json += "\"status\":\"already_running\"";
        DEBUG_CORE_WIFI("Scan already running\n");
    } else {
        WiFi.scanNetworks(true);
        json += "\"status\":\"started\"";
        DEBUG_CORE_WIFI("Scan triggered\n");
    }
    
    json += "}";
    request->send(200, "application/json", json);
}

void CLASS_CORE_WIFI::send_network_configuration_html(AsyncWebServerRequest *request) {
	// DEBUG_CORE_WIFI(__FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		//bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUG_CORE_WIFI("Arg %d: %s\r\n", i, request->arg(i).c_str());
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
		_applyWifiPending = true;   // применить конфиг в следующем тике loop

	}
	else {
		DEBUG_CORE_WIFI("%s\r\n", request->url().c_str());
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
	DEBUG_CORE_WIFI(__PRETTY_FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
}
// wifi.html ^^^

void CLASS_CORE_WIFI::send_slot_json(AsyncWebServerRequest *request, int slot) {
    DEBUG_CORE_WIFI("Sending slot %d data as JSON\n", slot);
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
        DEBUG_CORE_WIFI("Received body for slot %d: %s\n", slot, body.c_str());
        
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
            DEBUG_CORE_WIFI("JSON parse error\n");
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
            DEBUG_CORE_WIFI("Saved slot %d ok.\n", slot);
            this->_applyWifiPending = true;   // применить конфиг в следующем тике loop
        } else {
            request->send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            DEBUG_CORE_WIFI("Saved slot %d failed.\n", slot);
        }
    } else {
        DEBUG_CORE_WIFI("No body data for slot %d\n", slot);
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
    DEBUG_CORE_WIFI("send_wifi_sysconf_json\n");
    String values = "";
    values += "scantime_hours|" + String(_wifiScanTime / 60) + "|input\n";
    values += "scantime_mins|" + String(_wifiScanTime % 60) + "|input\n";
    values += "aptime|" + String(_wifiAPLifeTime) + "|input\n";
    values += "busmode|" + String((int)_busMode) + "|input\n";
    values += "scan_retries|" + String((int)_scanRetries) + "|input\n";
    request->send(200, "text/plain", values);
}

void CLASS_CORE_WIFI::handle_wifi_sysconf_post(AsyncWebServerRequest *request) {
    DEBUG_CORE_WIFI("handle_wifi_sysconf_post\n");
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

    // busmode/scan_retries — опционально: применяем только если ключ реально присутствует,
    // чтобы старые клиенты (без этих полей) не сбросили значения в дефолт.
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (!err) {
            if (doc["busmode"].is<int>()) {
                int32_t busmodeVal = doc["busmode"].as<int32_t>();
                if (busmodeVal != WIFI_MODE_AUTO && busmodeVal != WIFI_MODE_MACRO) busmodeVal = WIFI_MODE_AUTO;
                setWifiMode((uint8_t)busmodeVal);
            }
            if (doc["scan_retries"].is<int>()) {
                int32_t sr = doc["scan_retries"].as<int32_t>();
                if (sr < WIFI_SCAN_RETRIES_MIN) sr = WIFI_SCAN_RETRIES_MIN;
                if (sr > WIFI_SCAN_RETRIES_MAX) sr = WIFI_SCAN_RETRIES_MAX;
                setScanRetries((uint8_t)sr);
            }
        }
    }

    if (save_configWifiSys()) {
        scanTime = _wifiScanTime * MINUTES;
        request->send(200, "application/json", "{\"success\":true}");
        DEBUG_CORE_WIFI("WiFi sys config saved: scantime=%d, aptime=%d, busmode=%d, scan_retries=%d\n",
                        _wifiScanTime, _wifiAPLifeTime, (int)_busMode, (int)_scanRetries);
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
	DEBUG_CORE_WIFI("Save config\r\n");
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	// Синхронизируем in-memory SSID слота: scanWifi() сравнивает по _strWifiN,
	// иначе новый SSID подхватится только после перезагрузки.
	if (_in == 0)  { sprintf(_strWifi0, "%s", _wifiConfig.ssid.c_str()); _wifiFailCount[0] = 0; }
	if (_in == 1)  { sprintf(_strWifi1, "%s", _wifiConfig.ssid.c_str()); _wifiFailCount[1] = 0; }
	if (_in == 2)  { sprintf(_strWifi2, "%s", _wifiConfig.ssid.c_str()); _wifiFailCount[2] = 0; }
	if (_in == 3)  { sprintf(_strWifi3, "%s", _wifiConfig.ssid.c_str()); _wifiFailCount[3] = 0; }
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
	DEBUG_CORE_WIFI(__PRETTY_FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
}

bool CLASS_CORE_WIFI::load_configWifiSys() {
    DEBUG_CORE_WIFI("Loading WiFi sys config\n");
    JsonDocument doc;
    if (!core_json.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc)) return false;
    _wifiScanTime = doc["scantime"].as<uint16_t>();
    _wifiAPLifeTime = doc["aptime"].as<uint16_t>();
    _busMode = doc["busmode"].as<uint8_t>() & 1;
    int32_t sr = doc["scan_retries"] | 10;
    if (sr < WIFI_SCAN_RETRIES_MIN) sr = WIFI_SCAN_RETRIES_MIN;
    if (sr > WIFI_SCAN_RETRIES_MAX) sr = WIFI_SCAN_RETRIES_MAX;
    _scanRetries = (uint8_t)sr;
    return true;
}

bool CLASS_CORE_WIFI::save_configWifiSys() {
    DEBUG_CORE_WIFI("Saving WiFi sys config\n");
    JsonDocument doc;
    core_json.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc);
    doc["scantime"] = _wifiScanTime;
    doc["aptime"] = _wifiAPLifeTime;
    doc["busmode"] = _busMode;
    doc["scan_retries"] = _scanRetries;
    return core_json.jsonFileSaveDoc(WIFI_CONFIG_SYS, doc);
}

void CLASS_CORE_WIFI::defaultConfigWifiSys() {
    DEBUG_CORE_WIFI("defaultConfigWifiSys\n");
    _wifiScanTime = 1;
    _wifiAPLifeTime = 10;
    _busMode = WIFI_MODE_AUTO;
    _scanRetries = 10;
}

// ============================================================
// Отложенное сохранение (bus-функция wifi.save)
// ============================================================
void CLASS_CORE_WIFI::saveNow() {
    if (_pendingSave) return;   // guard от дублей: EERTOS SetTask не идемпотентна
    _pendingSave = true;
    SetTask(&CLASS_CORE_WIFI::s_deferredSave);
}

void CLASS_CORE_WIFI::s_deferredSave() {
    core_wifi.deferredSave();
}

void CLASS_CORE_WIFI::deferredSave() {
    _pendingSave = false;
    save_configWifiSys();
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
    DEBUG_CORE_WIFI("%s\n\r", __FUNCTION__);
    String values = "";
    values += "wifiversion|"     + getVersionStr()    + "|div\n";
    values += "wifigentime|"     + getGeneratedTime() + "|div\n";
    values += "wifigendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
