#include "main.h"
#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#include <esp_wifi.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#include <avr/pgmspace.h>
#endif

#include <DNSServer.h>

#include "core_web/FSWebServerLib.h"

#include "core_wifi/core_wifi.h"
#include "core_sys/core_sys.h"
#include "core_ntp/core_ntp.h"
#include "core_led/core_led.h"
#include "core_state/core_state.h"
#include "core_sys/eertos.h"

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#ifdef MODULE_OTACLIENT
#include "module_otaclient/module_otaclient.h"
#endif

// ============================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПЕРЕМЕННЫЕ
// ============================================================

DNSServer 		dnsServer;

static const char patWifiScan[]     PROGMEM = "*.*.*.*.";
static const char patWifiDisc[]     PROGMEM = "*.........";
static const char patWifiAP[]       PROGMEM = "*.*.*......";
static const char patWifiConn[]     PROGMEM = "*.*..";
static const char patWifiErr[]      PROGMEM = "*.*.*";

// ============================================================
// КОНКРЕТНАЯ ЛОГИКА МОДУЛЯ
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
			DEBUG_CORE_WIFI("Periodic reset of wifi fail counters\n");
			resetWifiFailCounters();
		}
	}

	// Применение только что сохранённого конфига Wi-Fi (отложено из web-обработчика)
	if (_applyWifiPending) {
		_applyWifiPending = false;
		applyWifiConfigNow();
		return;
	}

	// В macro-режиме целью управляет автомат согласно _target (см. applyMacroTarget).
	if (_busMode == WIFI_MODE_MACRO) {
		applyMacroTarget();
		return;
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
	// --- macro-режим: целевой AP или ожидание освобождения AP (G3) ---
	if (_busMode == WIFI_MODE_MACRO) {
		// Ожидание перехода в STA (5j): клиенты ещё подключены, ждём освобождения.
		if (_target == WIFI_TARGET_STA && _pendingStaSwitch) {
			if (_apClientCount == 0) {
				_pendingStaSwitch = false;
				applyStaSwitch();
				return;
			}
			uint32_t waitSec = (_apHoldMin > 0) ? (_apHoldMin * 60) : WIFI_AP_HOLD_DEFAULT_SEC;
			if ((uint32_t)(_stateSeconds - _pendingStaSince) >= waitSec) {
				DEBUG_CORE_WIFI("STA switch timeout, kicking AP clients\r\n");
				kickApClients();
				_apClientCount = 0;
				core_state.signal("wifi.ap_clients", BusValue::i32(0));
				core_state.signal("wifi.ap_busy", BusValue::bo(false));
				_pendingStaSwitch = false;
				applyStaSwitch();
				return;
			}
			ledMacrosWifiAP();
			return;
		}
		// Целевой AP (5e): время держания — только _apHoldMin (не _wifiAPLifeTime).
		if (_target == WIFI_TARGET_AP) {
			if (_apHoldMin == 0) {
				ledMacrosWifiAP();
				return;
			}
			if (_apClientActivity) {
				_apClientActivity = false;
				_apUptime = 0;
				ledMacrosWifiAP();
				return;
			}
			if ((uint32_t)++_apUptime >= _apHoldMin * 60) {
				DEBUG_CORE_WIFI("AP hold expired. Restarting AP.\r\n");
				_apUptime = 0;
				_apClientActivity = false;
				configureWifiAP();
				return;
			}
			ledMacrosWifiAP();
			return;
		}
		// macro, но не целевой AP и нет ожидания — не ожидается; проваливаемся в автономку.
	}

	// --- автономка (auto) — без изменений ---
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
		DEBUG_CORE_WIFI("AP idle %lu sec without activity. Leaving AP to scan.\r\n", (unsigned long)_wifiAPLifeTime * 60);
		leaveApToScan();
		return;
	}
	ledMacrosWifiAP();
}

void CLASS_CORE_WIFI::leaveApToScan() {
	DEBUG_CORE_WIFI("AP -> STA scan\r\n");
	dnsServer.stop();
	_suppressDisc = 3;   // события от переключения режимов игнорируем
	_ignoreDisconnect = true;
	WiFi.softAPdisconnect(false);
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	_apClientCount = 0;
	core_state.signal("wifi.ap_mode", BusValue::bo(false));
	core_state.signal("wifi.ap_clients", BusValue::i32(0));
	core_state.signal("wifi.ap_busy", BusValue::bo(false));
	_scanActive = true;
	_apScanPhaseUntil = _stateSeconds + WIFI_AP_RETRY_PHASE_SEC;
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
	_scanActive = false;
	_apScanPhaseUntil = 0;
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
			DEBUG_CORE_WIFI("Connect budget expired.\r\n");
			if (_busMode == WIFI_MODE_MACRO && (_target == WIFI_TARGET_STA || _scanSeriesLimit > 0)) {
				// В macro-цели STA (или активной серии force_scan) бюджет коннекта
				// истёк — возвращаемся к скану, не в AP.
				WifiScan = WF_STAT_SCANING;
				connectionTimout = 0;
				_scanActive = false;
				_nextStaScanAt = _stateSeconds;
				return;
			}
			enterApWait();
		}
		return;
	}

	// Пауза между повторными сканами (STA-режим без AP, сеть не находится)
	if (_stateSeconds < _nextStaScanAt) {
		ledMacrosWifiScan();
		return;
	}

	// Скан ещё не запущен — запускаем асинхронный скан
	if (!_scanActive) {
		_scanActive = true;
		connectionTimout = 0;
		WiFi.scanNetworks(true);
		ledMacrosWifiScan();
		return;
	}

	int st = WiFi.scanComplete();
	if (st == WIFI_SCAN_RUNNING) {
		// Защита от «зависшего» скана: если скан не завершается дольше порога —
		// перезапускаем через AP-ожидание или повторный скан
		if ((uint32_t)++connectionTimout >= WIFI_SCAN_STUCK_SEC) {
			DEBUG_CORE_WIFI("Scan stuck %lu sec. Restarting.\r\n", (unsigned long)WIFI_SCAN_STUCK_SEC);
			connectionTimout = 0;
			WiFi.scanDelete();
			_scanActive = false;
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
		// Скан/драйвер не поднялся. Не дёргаем WiFi.scanNetworks() каждую
		// секунду — иначе esp_wifi_init зацикливается. Работаем с бэкоффом:
		// одна повторная попытка за окно WIFI_INIT_FAIL_PAUSE_SEC.
		connectionTimout = 0;
		_wifiInitFailCount++;
		if (_wifiInitFailCount >= WIFI_INIT_FAIL_MAX) {
			int mode = core_state.getMode();
			if (mode == CORE_MODE_NORMAL || mode == CORE_MODE_INIT) {
				DEBUG_CORE_WIFI("WiFi init failed %d times. Restarting.\r\n", _wifiInitFailCount);
				ESP.restart();
				return;
			}
			// Длительная операция (OTA/FS/prog) — рестарт откладываем
			DEBUG_CORE_WIFI("WiFi init failed %d times, restart deferred (mode %d)\r\n",
			           _wifiInitFailCount, mode);
		}
		// Не дёргаем драйвер каждую секунду: ждём окно бэкоффа, затем пробуем снова
		WiFi.scanDelete();
		_scanActive = false;
		_nextStaScanAt = _stateSeconds + WIFI_INIT_FAIL_PAUSE_SEC;
		ledMacrosWifiScan();
		return;
	}
	if (st < 0) {
		ledMacrosWifiScan();
		return;
	}

	// Скан завершён
	_scanActive = false;
	connectionTimout = 0;
	_wifiInitFailCount = 0;
	int slot = (st > 0) ? scanWifi() : -1;
	if (st > 0) { WiFi.scanDelete(); }
	if (slot < 0) {
		// Сети из конфигов нет (или все SSID заблокированы счётчиками неудач).
		// В macro-цели STA действует правило скан-серии (Q18): терпение измеряется
		// числом пустых сканов (scan_retries / лимит force_scan), а не минутами.
		if (_busMode == WIFI_MODE_MACRO && (_target == WIFI_TARGET_STA || _scanSeriesLimit > 0)) {
			_scanEmptyCount++;
			uint32_t limit = (_scanSeriesLimit > 0) ? _scanSeriesLimit : _scanRetries;
			if (_seriesHadIp) {
				// Уже была связь — держим STA, рескан с паузой. Хозяин — макрос.
				_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
				ledMacrosWifiScan();
				return;
			}
			if (_scanEmptyCount >= limit) {
				_scanEmptyCount = 0;
				if (_scanSeriesLimit > 0) {
					// Серия force_scan исчерпана — возврат к прежней цели (C1).
					_scanSeriesLimit = 0;
					uint8_t prev = _targetBeforeForceScan;
					_targetBeforeForceScan = WIFI_TARGET_AUTO;
					if (prev == WIFI_TARGET_AP) {
						configureWifiAP();
						return;
					}
					// prev == STA или AUTO — продолжаем штатный STA-цикл.
					_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
					ledMacrosWifiScan();
					return;
				}
				// Обычный порог Q18 (target=STA): сети реально нет — фолбэк в AP.
				DEBUG_CORE_WIFI("No network after %lu scans. Fallback to AP.\r\n", (unsigned long)limit);
				configureWifiAP();
				return;
			}
			_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
			ledMacrosWifiScan();
			return;
		}

		// --- автономка (auto) — без изменений ---
		// Если после выхода из AP скан-фаза ещё не истекла — продолжаем сканировать,
		// иначе возвращаемся в AP (или ждём следующего скана при выключенном AP).
		if (_wifiAPLifeTime > 0 && _stateSeconds >= _apScanPhaseUntil) {
			_apScanPhaseUntil = 0;
			enterApWait();
			return;
		}
		_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
		ledMacrosWifiScan();
		return;
	}

	// Найдена сеть из конфигов — подключаемся
	load_configWifi(slot);
	WifiScan = WF_SCAN_NO_NEED;
	connectionTimout = 0;
	_scanEmptyCount = 0;   // успешный скан сбрасывает счётчик пустых сканов
	DEBUG_CORE_WIFI("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
	WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	ledMacrosWifiConnecting();
}

void CLASS_CORE_WIFI::startDNSCaptive() {
    // Перехватываем все DNS запросы и направляем на IP точки доступа
    dnsServer.start(53, "*", WiFi.softAPIP());
    DEBUG_CORE_WIFI("DNS captive portal started on port 53\n");
}

void CLASS_CORE_WIFI::configureWifiAP() {
	DEBUG_CORE_WIFI(__PRETTY_FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
	core_ntp.ntpOnDisconected();
#if defined(MODULE_UDP)
		module_udp.stop();	// always stop!
#endif
	String APname = core_sys.getHostName();
	_suppressDisc = 3;   // события от собственного отключения STA игнорируем
	_ignoreDisconnect = true;
	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_APMODE;
	if (core_sys.httpAuthEnabled()) {
		WiFi.softAP(APname, core_sys.getHttpPassword());
		DEBUG_CORE_WIFI("AP Pass enabled: %s \r\n", core_sys.getHttpPassword().c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUG_CORE_WIFI("AP Pass disabled \r\n");
	}
	startDNSCaptive();
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 5, 250);	}
	DEBUG_CORE_WIFI("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	_scanActive = false;
	_apScanPhaseUntil = 0;
	_apClientCount = 0;
	core_state.signal("wifi.ap_mode", BusValue::bo(true));
	core_state.signal("wifi.ap_clients", BusValue::i32(0));
	core_state.signal("wifi.ap_busy", BusValue::bo(false));
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
	DEBUG_CORE_WIFI(__PRETTY_FUNCTION__);	DEBUG_CORE_WIFI("\r\n");
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
	_scanActive = true;
	_apScanPhaseUntil = 0;
	_nextStaScanAt = _stateSeconds;
	WiFi.scanNetworks(true);
	ledMacrosWifiScan();
}

// Применить только что сохранённый слот Wi-Fi: сразу пересканировать сеть,
// чтобы новый SSID/пароль подхватились без перезагрузки.
void CLASS_CORE_WIFI::applyWifiConfigNow() {
	if (wifiStatus == FS_STAT_APMODE) {
		leaveApToScan();
	} else {
		configureWifi();
	}
}

#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiConnected()
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiConnected(WiFiEventStationModeConnected data)
#endif
{
	DEBUG_CORE_WIFI("WiFi Connected: Waiting for DHCP\n\r");
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

	DEBUG_CORE_WIFI("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DEBUG_CORE_WIFI("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUG_CORE_WIFI("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	wifiDisconnectedSince = 0;
	connectionTimout = 0;
	_wifiInitFailCount = 0;
	_scanActive = false;
	_apScanPhaseUntil = 0;
	wifiStatus = FS_STAT_CONNECTED;
	_enterApPending = false;
	_suppressDisc = 0;

	// Успешное подключение завершает текущую серию скана (Q18).
	_seriesHadIp = true;
	_scanEmptyCount = 0;
	_scanSeriesLimit = 0;
	_targetBeforeForceScan = WIFI_TARGET_AUTO;

	core_state.signal("wifi.connected", BusValue::bo(true));
	core_state.signal("wifi.rssi", BusValue::i32((int32_t)WiFi.RSSI()));
	core_state.signal("wifi.ip", BusValue::str(WiFi.localIP().toString()));
	core_state.signal("wifi.slot_name", BusValue::str(WiFi.SSID()));
	core_state.signal("wifi.ap_mode", BusValue::bo(false));
	core_state.emit("wifi.just_connected");
	if (_target == WIFI_TARGET_STA) {
		core_state.emit("wifi.target_reached");
	}

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
	DEBUG_CORE_WIFI("STA disconnected, reason: %u\r\n", (unsigned)reason);

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
	DEBUG_CORE_WIFI("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));

	wifiStatus = FS_STAT_CONNECTING;
	connectionTimout = 0;
	_scanActive = false;
	_apScanPhaseUntil = 0;

	core_state.signal("wifi.connected", BusValue::bo(false));
	core_state.signal("wifi.slot_name", BusValue::str(""));
	core_state.emit("wifi.just_disconnected");

	if (authFail) {
		DEBUG_CORE_WIFI("Auth fail (wrong password?): %s\r\n", _wifiConfig.ssid.c_str());
		wifiSsidSetPSWDwrong(_wifiConfig.ssid);
		ledMacrosWifiDisconnect();
		_enterApPending = false;   // отменяем отложенный вход в AP от предыдущего события разрыва
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
	DEBUG_CORE_WIFI("wifi ssid wrong password: %s \n", _str.c_str());
	// Вместо безвозвратного удаления SSID — инкрементируем счётчик неудач
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	_wifiFailCount[3]++; }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	_wifiFailCount[2]++; }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	_wifiFailCount[1]++; }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	_wifiFailCount[0]++; }
}

void CLASS_CORE_WIFI::resetWifiFailCounters() {
	DEBUG_CORE_WIFI("resetWifiFailCounters\n");
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

// ============================================================
// Светодиодная индикация статуса Wi-Fi
// ============================================================

void ledMacrosWifiScan()			{	ledSetState(LED_PRIO_WIFI, patWifiScan, 10);  }
void ledMacrosWifiDisconnect()		{	ledSetState(LED_PRIO_WIFI, patWifiDisc, 3);  }
void ledMacrosWifiAP()				{	ledSetState(LED_PRIO_WIFI, patWifiAP, -1); }
void ledMacrosWifiConnecting()		{	ledSetState(LED_PRIO_WIFI, patWifiConn, 2); }
void ledMacrosWifiError()			{	ledSetState(LED_PRIO_WIFI, patWifiErr, 5); }
void ledMacrosWifiConnected()		{	ledSetSteady(true); ledClearState(LED_PRIO_WIFI); }

// ============================================================
// РЕСУРСНАЯ ШИНА: режим/цель, applyMacroTarget и force-команды
// ============================================================

bool CLASS_CORE_WIFI::wifiBusAllowed() {
	return _busMode == WIFI_MODE_MACRO;
}

void CLASS_CORE_WIFI::setWifiMode(uint8_t m) {
	m &= 1;
	if (m == _busMode) return;
	_busMode = m;
	if (m == WIFI_MODE_AUTO) {
		_scanSeriesLimit = 0;
		_scanEmptyCount = 0;
		_apUptime = 0;
		_apClientActivity = false;
		_pendingStaSwitch = false;
	}
	core_state.signal("wifi.mode", BusValue::en((int32_t)_busMode));
}

void CLASS_CORE_WIFI::setTarget(uint8_t t) {
	if (_busMode != WIFI_MODE_MACRO) return;   // target вне macro игнорируется
	if (t > WIFI_TARGET_STA) return;
	if (t == _target) return;                  // self-assign no-op — серию не трогаем
	_target = t;
	core_state.signal("wifi.target", BusValue::en((int32_t)_target));
	if (t == WIFI_TARGET_AP) {
		_pendingStaSwitch = false;
		configureWifiAP();   // напрямую (не enterApWait): обрыв STA выполняется внутри
	} else if (t == WIFI_TARGET_STA) {
		// G3/G3a: при активных клиентах AP переход откладывается.
		if (_apClientCount > 0) {
			_pendingStaSwitch = true;
			_pendingStaSince = _stateSeconds;
			core_state.emit("wifi.sta_pending");
			return;
		}
		applyStaSwitch();
	} else {
		// AUTO: сброс ожиданий и серии, отдать управление автомату.
		_pendingStaSwitch = false;
		_scanSeriesLimit = 0;
		_scanEmptyCount = 0;
	}
}

void CLASS_CORE_WIFI::setSlot(const String& s) {
	_slot = s;
	core_state.signal("wifi.slot", BusValue::str(s));
}

void CLASS_CORE_WIFI::setApHoldMin(uint32_t m) {
	if (m > WIFI_AP_HOLD_MIN_MAX) m = WIFI_AP_HOLD_MIN_MAX;
	if (m == _apHoldMin) return;
	_apHoldMin = m;
	core_state.signal("wifi.ap_hold_min", BusValue::i32((int32_t)_apHoldMin));
}

void CLASS_CORE_WIFI::setScanRetries(uint8_t n) {
	if (n < WIFI_SCAN_RETRIES_MIN) n = WIFI_SCAN_RETRIES_MIN;
	if (n > WIFI_SCAN_RETRIES_MAX) n = WIFI_SCAN_RETRIES_MAX;
	if (n == _scanRetries) return;
	_scanRetries = n;
	core_state.signal("wifi.scan_retries", BusValue::i32((int32_t)_scanRetries));
}

void CLASS_CORE_WIFI::applyMacroTarget() {
	// Синхронизируем цель из шины (макросы пишут set("wifi.target", ...)).
	int32_t busTarget = core_state.getInt("wifi.target", (int32_t)_target);
	if (busTarget >= WIFI_TARGET_AUTO && busTarget <= WIFI_TARGET_STA && busTarget != (int32_t)_target) {
		setTarget((uint8_t)busTarget);
		return;   // setTarget выполнил переход/серию; продолжаем в следующем тике
	}

	// Синхронизируем слот/удержание/число сканов (сеттеры клампят и сигналят).
	String busSlot = core_state.getStr("wifi.slot", _slot);
	if (busSlot != _slot) { setSlot(busSlot); }
	int32_t busHold = core_state.getInt("wifi.ap_hold_min", (int32_t)_apHoldMin);
	if (busHold != (int32_t)_apHoldMin) { setApHoldMin((uint32_t)busHold); }
	int32_t busSr = core_state.getInt("wifi.scan_retries", (int32_t)_scanRetries);
	if (busSr != (int32_t)_scanRetries) { setScanRetries((uint8_t)busSr); }

	// Активная серия force_scan: временно сканируем в STA, игнорируя target (C1).
	if (_scanSeriesLimit > 0) {
		if (wifiStatus == FS_STAT_CONNECTED) {
			_scanSeriesLimit = 0;
			_scanEmptyCount = 0;
			_targetBeforeForceScan = WIFI_TARGET_AUTO;
			ledMacrosWifiConnected();
			return;
		}
		if (wifiStatus == FS_STAT_APMODE) {
			leaveApToScan();
			return;
		}
		staTick();
		return;
	}

	if (_target == WIFI_TARGET_AP) {
		if (wifiStatus == FS_STAT_APMODE) {
			dnsServer.processNextRequest();
			apTick();
		} else {
			configureWifiAP();
		}
		return;
	}

	if (_target == WIFI_TARGET_STA) {
		if (_pendingStaSwitch) {
			dnsServer.processNextRequest();
			apTick();
			return;
		}
		if (wifiStatus == FS_STAT_APMODE) {
			// Фолбэк-AP после исчерпания сканов: держим AP, пока макрос не сменит цель.
			dnsServer.processNextRequest();
			ledMacrosWifiAP();
			return;
		}
		if (wifiStatus == FS_STAT_CONNECTED) {
			ledMacrosWifiConnected();
			return;
		}
		staTick();
		return;
	}

	// _target == AUTO (в macro не применяется, но на всякий случай) — штатный автомат.
	if (wifiStatus == FS_STAT_APMODE) {
		dnsServer.processNextRequest();
		apTick();
		return;
	}
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

void CLASS_CORE_WIFI::applyStaSwitch() {
	// Реальный переход в STA: покидаем AP (если были) и запускаем скан.
	leaveApToScan();
	_seriesHadIp = false;
	_scanEmptyCount = 0;
	_scanSeriesLimit = 0;
	_targetBeforeForceScan = WIFI_TARGET_AUTO;
	core_state.emit("wifi.sta_applied");
}

int CLASS_CORE_WIFI::resolveTargetSlot() {
	if (_slot.length() == 0) return -1;   // "" = авто
	if (_slot.length() == 1 && _slot[0] >= '0' && _slot[0] <= '3') {
		return (int)(_slot[0] - '0');     // явный номер слота
	}
	if (strcmp(_strWifi0, _slot.c_str()) == 0) return 0;
	if (strcmp(_strWifi1, _slot.c_str()) == 0) return 1;
	if (strcmp(_strWifi2, _slot.c_str()) == 0) return 2;
	if (strcmp(_strWifi3, _slot.c_str()) == 0) return 3;
	return -1;   // SSID не найден ни в одном слоте
}

void CLASS_CORE_WIFI::kickApClients() {
#if defined(ESP32)
	esp_wifi_deauth_sta(0);   // мягкий deauth всех клиентов AP
#endif
#if defined(ESP8266)
	WiFi.softAPdisconnect(false);   // перезапуск softAP — клиенты отваливаются (1-3 с)
#endif
}

void CLASS_CORE_WIFI::onApStationConnected() {
	_apClientCount++;
	core_state.signal("wifi.ap_clients", BusValue::i32((int32_t)_apClientCount));
	core_state.signal("wifi.ap_busy", BusValue::bo(_apClientCount > 0));
	core_state.emit("wifi.ap_client_joined");
}

void CLASS_CORE_WIFI::onApStationDisconnected() {
	if (_apClientCount) { _apClientCount--; }
	core_state.signal("wifi.ap_clients", BusValue::i32((int32_t)_apClientCount));
	core_state.signal("wifi.ap_busy", BusValue::bo(_apClientCount > 0));
	core_state.emit("wifi.ap_client_left");
}

int CLASS_CORE_WIFI::forceAp() {
	if (_apClientCount > 0) return BUS_ERR_BUSY;   // безопасно: клиент подключён — no-op
	_target = WIFI_TARGET_AP;
	core_state.signal("wifi.target", BusValue::en((int32_t)_target));
	_pendingStaSwitch = false;
	configureWifiAP();
	return BUS_OK;
}

int CLASS_CORE_WIFI::forceApKick() {
	kickApClients();
	_apClientCount = 0;
	core_state.signal("wifi.ap_clients", BusValue::i32(0));
	core_state.signal("wifi.ap_busy", BusValue::bo(false));
	_target = WIFI_TARGET_AP;
	core_state.signal("wifi.target", BusValue::en((int32_t)_target));
	_pendingStaSwitch = false;
	configureWifiAP();
	return BUS_OK;
}

int CLASS_CORE_WIFI::forceConnect() {
	if (_apClientCount > 0) return BUS_ERR_BUSY;
	int slot = resolveTargetSlot();
	if (slot < 0) return BUS_ERR_NOT_FOUND;
	load_configWifi(slot);
	if (_wifiConfig.ssid.length() == 0 || _wifiConfig.password.length() == 0) {
		return BUS_ERR_NOT_READY;   // слот найден, но не заполнен
	}
	// Переход в STA и прямое подключение к выбранному слоту (без скана).
	_suppressDisc = 3;
	_ignoreDisconnect = true;
	if (wifiStatus == FS_STAT_APMODE) {
		dnsServer.stop();
		WiFi.softAPdisconnect(false);
	}
	if (WiFi.isConnected()) { WiFi.disconnect(); }
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_SCAN_NO_NEED;
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	_apClientCount = 0;
	_scanActive = false;
	_apScanPhaseUntil = 0;
	_seriesHadIp = false;
	_scanEmptyCount = 0;
	_scanSeriesLimit = 0;
	_targetBeforeForceScan = WIFI_TARGET_AUTO;
	_target = WIFI_TARGET_STA;   // force_connect подразумевает цель STA
	core_state.signal("wifi.target", BusValue::en((int32_t)_target));
	core_state.signal("wifi.ap_mode", BusValue::bo(false));
	core_state.signal("wifi.ap_clients", BusValue::i32(0));
	core_state.signal("wifi.ap_busy", BusValue::bo(false));
	DEBUG_CORE_WIFI("force_connect: connecting to %s\r\n", _wifiConfig.ssid.c_str());
	WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	ledMacrosWifiConnecting();
	return BUS_OK;
}

int CLASS_CORE_WIFI::forceConnectKick() {
	if (_apClientCount > 0) {
		kickApClients();
		_apClientCount = 0;
		core_state.signal("wifi.ap_clients", BusValue::i32(0));
		core_state.signal("wifi.ap_busy", BusValue::bo(false));
	}
	return forceConnect();
}

int CLASS_CORE_WIFI::forceScan(int n) {
	if (n < WIFI_SCAN_RETRIES_MIN) n = WIFI_SCAN_RETRIES_MIN;
	if (n > WIFI_SCAN_RETRIES_MAX) n = WIFI_SCAN_RETRIES_MAX;
	_scanSeriesLimit = (uint8_t)n;
	_scanEmptyCount = 0;
	_seriesHadIp = false;
	_targetBeforeForceScan = _target;   // цель не меняем (C1)
	// Переводим в STA-скан, если ещё не в STA.
	if (wifiStatus == FS_STAT_APMODE) {
		leaveApToScan();
	} else {
		rescanSoon();
	}
	return BUS_OK;
}

int CLASS_CORE_WIFI::forceDisconnect() {
	_suppressDisc = 3;
	_ignoreDisconnect = true;
	WiFi.disconnect();
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	_scanActive = false;
	core_state.signal("wifi.connected", BusValue::bo(false));
	core_state.signal("wifi.slot_name", BusValue::str(""));
	core_state.emit("wifi.just_disconnected");
	return BUS_OK;
}
