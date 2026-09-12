#include "core_sys.h"
#include "version.h"

#if defined(ESP32)
#include <LittleFS.h>
#include <esp_task_wdt.h>
#endif

#if defined(ESP8266)
#include <LittleFS.h>
#endif

#include "debug.h"
#include "common/common.h"
#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"
#include "core_sys/ident_store.h"
#include "core_sys/common_module.h"
#include "core_state/core_state.h"

CLASS_CORE_SYS core_sys;

// ============================================================
// begin()
// ============================================================

void CLASS_CORE_SYS::begin(ModContext& ctx) {
	_fs = ctx.fs;
	loadHTTPAuth();
	defaultConfigSys();
	bool fsSysCfg = load_config_Sys();
	loadDeviceIdent(fsSysCfg);

	// Заполняем поля глобального контекста после загрузки identity/auth,
	// чтобы core_wifi и mDNS получили корректные hostname/password.
	ctx.hostname = getHostName();
	ctx.password = _httpAuth.wwwPassword;

	core_state.signal("system.hostname", BusValue::str(ctx.hostname));
	core_state.signal("system.safe", BusValue::bo(false));
	core_state.signal("system.idle", BusValue::bo(false));
}

// ============================================================
// register_resources()
// ============================================================
void CLASS_CORE_SYS::register_resources() {
	DEBUGSYS("%s\r\n", __FUNCTION__);

	core_state.regState("safe", BusValue::BOOL, "safe flag (parallel to any mode)", false);
	core_state.regState("idle", BusValue::BOOL, "idle flag (parallel to any mode)", false);
	core_state.regState("hostname", BusValue::STR, "device hostname", false);
}

// ============================================================
// web_Init()
// ============================================================

void CLASS_CORE_SYS::web_Init() {
	ESPHTTPServer.on("/system/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		DEBUGSYS("%s\r\n", request->url().c_str());
		request->send_P(200, "text/html", Page_IndexRefresh);
		ESPHTTPServer.restart_esp();
	});

	ESPHTTPServer.on("/system/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_wwwauth_configuration_values_html(request);
	});

	ESPHTTPServer.on("/system/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_send_chipinfo(request);
	});

	ESPHTTPServer.on("/system/version", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_version_info(request);
	});

	ESPHTTPServer.on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_system_Save(request);
	});

	ESPHTTPServer.on("/system/savewwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->set_wwwauth_configuration(request);
	});

	ESPHTTPServer.on("/system/devconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_system_Load(request);
	});

	// recover (восстановление забытого пароля) — публичные маршруты без авторизации
	ESPHTTPServer.on("/recover", HTTP_GET, [this](AsyncWebServerRequest *request) {
		core_wifi.notifyApClientActivity();
		if (!ESPHTTPServer.handleFileRead("/recover.html", request)) { request->send(404, "text/plain", "FileNotFound"); }
	});

	ESPHTTPServer.on("/recover/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
		core_wifi.notifyApClientActivity();
		this->recover_status_values_html(request);
	});

	ESPHTTPServer.on("/recover/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
		core_wifi.notifyApClientActivity();
		this->recover_reset(request);
	});
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_SYS::html_system_Load(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	String values = "";
	values += "name|"		+ _sysConfig.deviceName		+ "|input\n";
	values += "serial|" 	+ _sysConfig.deviceSerial 	+ "|input\n";
	request->send(200, "text/plain", values);
}

void CLASS_CORE_SYS::html_system_Save(AsyncWebServerRequest *request) {
	DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	if (request->args() > 0) { // Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGSYS("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "name") 		{ _sysConfig.deviceName 	= urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "serial") 	{ _sysConfig.deviceSerial 	= urldecode(request->arg(i));	continue; }
		}
		request->send_P(200, "text/html", Page_GeneralSys);
		save_configSys();
		saveSysIdentStore();
	}
	else {	ESPHTTPServer.handleFileRead(request->url(), request);	}
}

void CLASS_CORE_SYS::html_send_chipinfo(AsyncWebServerRequest *request) {
	DEBUGSYS(__FUNCTION__); DEBUGSYS("\r\n");

#if defined(ESP8266)
	ESP.wdtFeed();
	char buffer[256];
	snprintf(buffer, sizeof(buffer),
		"x_chipid|%s|div\n"
		"x_mhz|%d|div\n"
		"x_sdk|%s|div\n"
		"x_reason|%s|div\n",
		String(ESP.getChipId(), HEX).c_str(),
		ESP.getCpuFreqMHz(),
		ESP.getSdkVersion(),
		getResetReason().c_str()
	);
	request->send(200, "text/plain", buffer);
#endif
#if defined(ESP32)
	esp_task_wdt_reset();
	String values = "";
	values += "x_chipid|" + (String)ESP.getChipModel() + "|div\n";
	values += "x_mhz|" + (String)ESP.getCpuFreqMHz() + "|div\n";
	values += "x_sdk|" + (String)ESP.getSdkVersion() + "|div\n";
	values += "x_reason|" + getResetReason() + "|div\n";
	request->send(200, "text/plain", values);
#endif
}

void CLASS_CORE_SYS::html_version_info(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	String values = "";
	values += "devicename|"  	+ _sysConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" 	+ _sysConfig.deviceSerial 		+ "|div\n";
	values += "versionapp|" 	+ String(FIRMWARE_VERSION) + "|div\n";
	values += "versionfs|" 		+ getFsVersionStr() + "|div\n";

	values += "gitbranch|" ;values += GIT_BRANCH ;values += "|div\n";
	values += "gitcommit|" ;values += GIT_COMMIT ;values += "|div\n";
	values += "buildenv|" ;values += BUILD_ENV ;values += "|div\n";

	request->send(200, "text/plain", values);
}

void CLASS_CORE_SYS::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	String values = "";
	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";
	values += "wwwsecq|" + (String)_httpAuth.wwwQuestion + "|input\n";
	values += "wwwseca|" + (String)_httpAuth.wwwAnswer + "|input\n";

	request->send(200, "text/plain", values);
}

void CLASS_CORE_SYS::set_wwwauth_configuration(AsyncWebServerRequest *request)	{
	DEBUGSYS(__PRETTY_FUNCTION__);	DEBUGSYS("\r\n");
	DEBUGSYS("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0)	{
		bool save	   = false;
		bool oldAuth  = _httpAuth.auth;
		String oldUser = _httpAuth.wwwUsername;
		String oldPass = _httpAuth.wwwPassword;
		String oldSecQ = _httpAuth.wwwQuestion;
		String oldSecA = _httpAuth.wwwAnswer;
		bool newAuth  = false;
		String newUser = "";
		String newPass = "";
		bool hasSecQ  = false;
		bool hasSecA  = false;
		String newSecQ = "";
		String newSecA = "";
		for (uint8_t i = 0; i < request->args(); i++)		{
			if (request->argName(i) == "authconf") {
				save = true;
				continue;
			}
			if (request->argName(i) == "wwwuser") {
				newUser = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				newPass = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwsecq") {
				newSecQ = urldecode(request->arg(i));
				hasSecQ = true;
				continue;
			}
			if (request->argName(i) == "wwwseca") {
				newSecA = urldecode(request->arg(i));
				hasSecA = true;
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				newAuth = true;
				continue;
			}
		}
		if (!newAuth)		{
			newUser = "";
			newPass = "";
		}

		// Если форма не передала поля контрольного вопроса (например, старая
		// закэшированная страница) — секцию восстановления не трогаем.
		if (!hasSecQ || !hasSecA) {
			newSecQ = oldSecQ;
			newSecA = oldSecA;
		}

		// Серверная валидация (запасной уровень; форма проверяет на странице).
		// Строгая проверка нового пароля выполняется только если он реально изменился,
		// чтобы не блокировать пароли, заданные до введения ограничений.
		if (newAuth) {
			if (newUser.length() == 0) {
				request->send(400, "text/plain", "User must be filled");
				return;
			}
			if (newPass != oldPass && !ns_core_sys::isAdminPassValid(newPass)) {
				request->send(400, "text/plain", "Password: 8-63 chars, only latin letters and digits");
				return;
			}
		}

		// Контрольный вопрос/ответ: допустимы только оба заполнены или оба пусты
		if ((newSecQ.length() == 0) != (newSecA.length() == 0)) {
			request->send(400, "text/plain", "Fill both question and answer or clear both");
			return;
		}

		_httpAuth.auth = newAuth;
		_httpAuth.wwwUsername = newUser;
		_httpAuth.wwwPassword = newPass;
		_httpAuth.wwwQuestion = newSecQ;
		_httpAuth.wwwAnswer = newSecA;

		if (save)		{
			saveHTTPAuth();
			bool changed = (oldAuth != newAuth) || (oldUser != newUser) || (oldPass != newPass)
				|| (oldSecQ != newSecQ) || (oldSecA != newSecA);
			if (changed == true) {
				// Применяем новый/убранный пароль ко всем механизмам (HTTP auth, AP, OTA):
				// ArduinoOTA фиксирует пароль при старте, поэтому требуется перезагрузка.
				request->send_P(200, "text/html", Page_IndexRefresh);
				ESPHTTPServer.restart_esp();
			}
			else {
				request->send_P(200, "text/html", Page_GeneralSys);
			}
		}
	}
}

// Статус восстановления для публичной страницы /recover (формат ApplyCVT)
void CLASS_CORE_SYS::recover_status_values_html(AsyncWebServerRequest *request) {
	DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	String values = "";
	if (_httpAuth.wwwQuestion.length() > 0 && _httpAuth.wwwAnswer.length() > 0) {
		values += "recover_cfg|1|div\n";
		values += "recover_q|" + escapeHtml(_httpAuth.wwwQuestion) + "|div\n";
	}
	else {
		values += "recover_cfg|0|div\n";
		values += "recover_q||div\n";
	}
	request->send(200, "text/plain", values);
}

// Сброс пароля по контрольному ответу (публичный эндпоинт, без авторизации)
void CLASS_CORE_SYS::recover_reset(AsyncWebServerRequest *request) {
	DEBUGSYS(__FUNCTION__);	DEBUGSYS("\r\n");
	if (_httpAuth.wwwQuestion.length() == 0 || _httpAuth.wwwAnswer.length() == 0) {
		request->send(200, "text/html", "<html><head><meta charset='utf-8'></head><body><h2>Recovery not configured</h2><a href='/recover'>Back</a></body></html>");
		return;
	}
	String answer = request->hasArg("answer") ? urldecode(request->arg("answer")) : "";
	if (answer != _httpAuth.wwwAnswer) {
		request->send(200, "text/html", "<html><head><meta charset='utf-8'></head><body><h2>Wrong answer</h2><a href='/recover'>Back</a></body></html>");
		return;
	}
	// Ответ совпал: стираем пароль полностью, вопрос/ответ сохраняем.
	// Перезагрузка нужна, чтобы отключение auth применилось к HTTP, AP и OTA.
	_httpAuth.auth = false;
	_httpAuth.wwwUsername = "";
	_httpAuth.wwwPassword = "";
	saveHTTPAuth();
	request->send_P(200, "text/html", Page_IndexRefresh);
	ESPHTTPServer.restart_esp();
}

// ============================================================
// Конфиг системы и identity
// ============================================================

bool CLASS_CORE_SYS::loadHTTPAuth() {
	DEBUGSYS(__PRETTY_FUNCTION__);	DEBUGSYS("\r\n");
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(SECRET_FILE, doc)) {
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		_httpAuth.wwwQuestion = "";
		_httpAuth.wwwAnswer = "";
		DEBUGSYS("Huh\n\r");
		return false;
	}
	_httpAuth.auth = doc["auth"].as<bool>();
	_httpAuth.wwwUsername = doc["user"].as<String>();
	_httpAuth.wwwPassword = doc["pass"].as<String>();
	_httpAuth.wwwQuestion = doc["secq"].as<String>();
	_httpAuth.wwwAnswer = doc["seca"].as<String>();
	DEBUGSYS(_httpAuth.auth ? "Secret initialized.\r\n" : "Auth disabled.\r\n");
	if (_httpAuth.auth) {
		DEBUGSYS("User: %s\r\n", _httpAuth.wwwUsername.c_str());
		DEBUGSYS("Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
	}
	return true;
}

bool CLASS_CORE_SYS::saveHTTPAuth() {
	DEBUGSYS("Save secret\r\n");
	JsonDocument doc;
	core_json.jsonFileLoadDoc(SECRET_FILE, doc);
	doc["auth"] = _httpAuth.auth;
	doc["user"] = _httpAuth.wwwUsername;
	doc["pass"] = _httpAuth.wwwPassword;
	doc["secq"] = _httpAuth.wwwQuestion;
	doc["seca"] = _httpAuth.wwwAnswer;
	return core_json.jsonFileSaveDoc(SECRET_FILE, doc);
}

bool CLASS_CORE_SYS::load_config_Sys() {
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(CONFIG_FILE_SYS, doc)) return false;
	_sysConfig.deviceName = doc["deviceName"].as<String>();
	_sysConfig.deviceSerial = doc["deviceSerial"].as<String>();
	return true;
}

void CLASS_CORE_SYS::defaultConfigSys() {
	// DEFAULT CONFIG SYSTEM
	// Серийник по умолчанию: последние 5 hex-символов уникального ID чипа
	// (короткий, но достаточно различимый для устройств с одинаковой прошивкой)
	#if defined(ESP32)
	_sysConfig.deviceName 		= "esp32";
	// Уникальный ID чипа из eFuse MAC (48 бит), hex без потери старших бит
	uint64_t chipId = ESP.getEfuseMac();
	char chipBuf[17];
	snprintf(chipBuf, sizeof(chipBuf), "%08lX%08lX",
		(unsigned long)(chipId >> 32), (unsigned long)(chipId & 0xFFFFFFFF));
	String chipStr = String(chipBuf);
	_sysConfig.deviceSerial = chipStr.substring(chipStr.length() > 5 ? chipStr.length() - 5 : 0);
	#endif
	#if defined(ESP8266)
	_sysConfig.deviceName 		= "esp8266";
	String chipStr = String(ESP.getChipId(), HEX);
	_sysConfig.deviceSerial = chipStr.substring(chipStr.length() > 5 ? chipStr.length() - 5 : 0);
	#endif
}

bool CLASS_CORE_SYS::saveSysIdentStore() {
	return identStoreSave(_sysConfig.deviceName, _sysConfig.deviceSerial);
}

void CLASS_CORE_SYS::loadDeviceIdent(bool fsOk) {
	String n = "";
	String s = "";
	if (identStoreLoad(n, s)) {
		// Энергонезависимое хранилище — источник истины (пережило обновление FS/прошивки)
		_sysConfig.deviceName = n;
		_sysConfig.deviceSerial = s;
		return;
	}
	// Хранилище пусто, повреждено или раздел отсутствует (старая partition table).
	// Если серийник из config_sys.json — заводская заглушка, формируем дефолт
	// платформы с уникальным ID чипа (новое устройство).
	bool factory = (_sysConfig.deviceSerial.length() == 0 || _sysConfig.deviceSerial == "000");
	if (factory) { defaultConfigSys(); }
	bool stored = saveSysIdentStore();
	// Записываем config_sys.json в FS, когда конфига не было вовсе, либо когда
	// идентичность только что инициализирована/мигрирована в хранилище (единоразово).
	if (stored || !fsOk) { save_configSys(); }
}

bool CLASS_CORE_SYS::save_configSys() {
	DEBUGSYS("Save config SYSTEM\r\n");
	JsonDocument doc;
	core_json.jsonFileLoadDoc(CONFIG_FILE_SYS, doc);
	doc["deviceName"] = _sysConfig.deviceName;
	doc["deviceSerial"] = _sysConfig.deviceSerial;
	return core_json.jsonFileSaveDoc(CONFIG_FILE_SYS, doc);
}

// ============================================================
// Идентичность и авторизация — публичное API
// ============================================================

const String CLASS_CORE_SYS::getHostName() { return _sysConfig.deviceName+"_"+_sysConfig.deviceSerial; }

String CLASS_CORE_SYS::getDeviceName() { return _sysConfig.deviceName; }

void CLASS_CORE_SYS::setDeviceName(const String& name) { _sysConfig.deviceName = name; }

String CLASS_CORE_SYS::getDeviceSerial() { return _sysConfig.deviceSerial; }

void CLASS_CORE_SYS::setDeviceSerial(const String& serial) { _sysConfig.deviceSerial = serial; }

bool CLASS_CORE_SYS::httpAuthEnabled() { return _httpAuth.auth; }

String CLASS_CORE_SYS::getHttpPassword() { return _httpAuth.wwwPassword; }

bool CLASS_CORE_SYS::checkAuth(AsyncWebServerRequest *request) {
	// Любой HTTP-запрос в AP-режиме считается активностью клиента
	// и продлевает «жизнь» AP (таймер в core_wifi)
	core_wifi.notifyApClientActivity();
	if (!_httpAuth.auth) {	return true;}
	else {
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}
}
