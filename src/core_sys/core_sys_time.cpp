#include "core_web/FSWebServerLib.h"

#include "core_sys.h"
#include "core_sys_version.h"
#include "common_module.h"

#include "debug.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "core_json/core_json.h"
#include "core_state/core_state.h"
#include "core_task/core_task.h"

#include <string.h>
#include <stdlib.h>

// ============================================================
// Свободные функции
// ============================================================

// Разбор bool из аргумента формы.
static bool timeParseBool(const String& v) {
	return (v == "1" || v == "true" || v == "on");
}

// Причина невалидности источника ("" если ок).
static const char* timeSourceReason(const strTimeSource& src) {
	if (src.status == nullptr) { return ""; }
	const char* why = src.status();
	return (why != nullptr) ? why : "";
}

// Причина для UI: живой status() либо последняя причина отказа из timeTick.
static const char* timeSourceUiReason(const strTimeSource& src) {
	const char* why = timeSourceReason(src);
	if (why != nullptr && why[0] != 0) { return why; }
	if (src.lastReason != nullptr && src.lastReason[0] != 0) { return src.lastReason; }
	return "";
}

// ============================================================
// Реестр источников
// ============================================================

int CLASS_CORE_SYS::findTimeSource(const char* name) const {
	if (name == nullptr) { return -1; }
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		if (strcmp(_timeSrc[i].name, name) == 0) { return (int)i; }
	}
	return -1;
}

bool CLASS_CORE_SYS::addTimeSource(const char* name, int prio,
                                   TimeGetFn get, TimeSetFn set, TimeStatusFn status) {
	if (name == nullptr || get == nullptr) { return false; }
	if (findTimeSource(name) >= 0) {
		DEBUGSYS("duplicate time source '%s'\r\n", name);
		return false;
	}
	if (_timeSrcCount >= CORE_SYS_TIME_MAX_SOURCES) {
		DEBUGSYS("time source list full, drop '%s'\r\n", name);
		return false;
	}

	strTimeSource& src = _timeSrc[_timeSrcCount];
	memset(&src, 0, sizeof(src));
	strncpy(src.name, name, CORE_SYS_TIME_NAME_LEN - 1);
	src.name[CORE_SYS_TIME_NAME_LEN - 1] = 0;
	src.prio = prio;
	src.enabled = true;
	src.get = get;
	src.set = set;
	src.status = status;
	src.validLast = false;
	src.lastOkMs = 0;
	src.registered = true;

	// Переопределения из config_time.json (sources.<name>.prio / .enabled).
	JsonDocument doc;
	if (core_json.jsonFileLoadDoc(CONFIG_FILE_TIME, doc)) {
		JsonObject sources = doc["sources"].as<JsonObject>();
		if (!sources.isNull()) {
			JsonObject o = sources[src.name].as<JsonObject>();
			if (!o.isNull()) {
				if (!o["prio"].isNull())    { src.prio = o["prio"].as<int32_t>(); }
				if (!o["enabled"].isNull()) { src.enabled = o["enabled"].as<bool>(); }
			}
		}
	}

	_timeSrcCount++;
	timeEmitSources();
	return true;
}

void CLASS_CORE_SYS::registerTimeResources() {
	core_state.setNamespace("time");
	core_state.regState("now",    BusValue::TIME, "system time (epoch) or fallback", false);
	core_state.regState("now_str",BusValue::STR,  "system time YYYY-MM-DD HH:MM:SS", false);
	core_state.regState("hour",   BusValue::I32,  "hour 0..23", false);
	core_state.regState("minute", BusValue::I32,  "minute 0..59", false);
	core_state.regState("second", BusValue::I32,  "second 0..59", false);
	core_state.regState("valid",  BusValue::BOOL, "at least one valid time source", false);
	core_state.regState("source", BusValue::STR,  "active source name ('' if none)", false);
	core_state.regState("source_count", BusValue::I32, "registered time sources", false);
	core_state.regState("sources",      BusValue::STR, "CSV of registered time sources", false);
	core_state.regState("tz",  BusValue::I32,  "timezone, tenths of hour (30 = +03:00)", true);
	core_state.regState("dst", BusValue::BOOL, "daylight saving", true);
	core_state.regEvent("synced",         "time became valid");
	core_state.regEvent("lost",           "no valid time sources");
	core_state.regEvent("source_changed", "active time source changed");
	core_state.regEvent("tz_changed",     "timezone or DST changed");
	core_state.regEvent("source_invalid", "time source became invalid");
	core_state.regFunc("set",       "TIME->", "set system time",          cmdTimeSet,      this);
	core_state.regFunc("sync_from", "STR->",  "force poll a source",      cmdTimeSyncFrom, this);
	core_state.regFunc("save",      "->",     "persist config_time.json", cmdTimeSave,     this);

	// Начальные значения производных ресурсов (config уже загружен в begin()).
	timeEmitSources();
	core_state.signal("time.now", BusValue::tm((int64_t)now()));
	timeUpdateDerived();
	core_state.signal("time.valid",  BusValue::bo(false));
	core_state.signal("time.source", BusValue::str(""));
	core_state.signal("time.tz",  BusValue::i32(_timeTzDec));
	core_state.signal("time.dst", BusValue::bo(_timeDst));

	core_state.setNamespace("system");
}

void CLASS_CORE_SYS::timeEmitSources() {
	String csv;
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		if (i > 0) { csv += ","; }
		csv += _timeSrc[i].name;
	}
	core_state.signal("time.source_count", BusValue::i32((int32_t)_timeSrcCount));
	core_state.signal("time.sources", BusValue::str(csv));
}

void CLASS_CORE_SYS::startTimeService() {
	if (_timeTickArmed) { return; }
	_timeTickArmed = true;
	core_task.every("time.tick", cbTimeTick, 1000, false);
}

// ============================================================
// Периодический опрос источников
// ============================================================

void CLASS_CORE_SYS::timeTick() {
	// 1. Порядок обхода — по prio desc (устойчиво по исходному порядку).
	int order[CORE_SYS_TIME_MAX_SOURCES];
	for (uint8_t i = 0; i < _timeSrcCount; i++) { order[i] = i; }
	for (uint8_t i = 1; i < _timeSrcCount; i++) {
		int key = order[i];
		int j = i - 1;
		while (j >= 0 && _timeSrc[order[j]].prio < _timeSrc[key].prio) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = key;
	}

	int activeIdx = -1;
	time_t activeT = 0;

	for (uint8_t k = 0; k < _timeSrcCount; k++) {
		int idx = order[k];
		strTimeSource& src = _timeSrc[idx];

		// 2. enabled
		if (!src.enabled) {
			src.lastReason = "disabled";
			if (src.validLast) {
				src.validLast = false;
				core_state.emit("time.source_invalid",
					BusValue::str(String(src.name) + ":disabled"));
			}
			continue;
		}

		// 3. status() первым (дешёвый health-check).
		if (src.status != nullptr) {
			const char* why = src.status();
			if (why != nullptr && why[0] != 0) {
				src.lastReason = why;
				if (src.validLast) {
					src.validLast = false;
					core_state.emit("time.source_invalid",
						BusValue::str(String(src.name) + ":" + why));
				}
				continue;
			}
		}

		// 4. get()
		time_t t = 0;
		if (!src.get(t)) {
			src.lastReason = "unavailable";
			if (src.validLast) {
				src.validLast = false;
				core_state.emit("time.source_invalid",
					BusValue::str(String(src.name) + ":unavailable"));
			}
			continue;
		}

		// 5. backward-jump (кроме первого валидного после старта и текущего активного:
		//    активный уже доверен, повторная проверка дала бы флап на «замороженном» чтении).
		if (_timeLastValid != 0 && (uint8_t)idx != _timeActiveIdx) {
			bool wasRecentlyValid = (_timeLastValid != 0)
				&& ((uint32_t)(millis() - _timeLastValidMs) < CORE_SYS_TIME_BACKJUMP_GRACE_S * 1000UL);
			if (!ns_core_sys::timeSelectBackJump(t, (time_t)now(), wasRecentlyValid)) {
				src.lastReason = "time in past (use time.sync_from)";
				if (src.validLast) {
					src.validLast = false;
					core_state.emit("time.source_invalid",
						BusValue::str(String(src.name) + ":time in past"));
				}
				continue;
			}
		}

		// 6. commit
		src.validLast = true;
		src.lastReason = nullptr;
		src.lastOkMs = millis();

		activeIdx = idx;
		activeT = t;
		break;   // ниже активного get() не вызываем
	}

	if (activeIdx >= 0) {
		bool first    = (_timeLastValid == 0);
		bool switched = (_timeActiveIdx != (uint8_t)activeIdx);

		// 7a. setTime только на переходе (первый успех / смена активного).
		if (first || switched) {
			setTime(activeT);
			_timeLastValid   = activeT;
			_timeActiveIdx   = (uint8_t)activeIdx;
			_timeLastRtcSync = millis() / 1000UL;
			core_state.signal("time.valid", BusValue::bo(true));
			core_state.signal("time.source", BusValue::str(_timeSrc[activeIdx].name));
			core_state.emit("time.source_changed");
			if (first) { core_state.emit("time.synced"); }
		}

		// 7b. Каждый такт при валиде.
		_timeLastValidMs = millis();
		core_state.signal("time.now", BusValue::tm((int64_t)now()));
		timeUpdateDerived();
	}
	else {
		// 8. Активный не найден.
		if (_timeActiveIdx != 255) {
			core_state.signal("time.valid", BusValue::bo(false));
			core_state.signal("time.source", BusValue::str(""));
			core_state.emit("time.lost");
			_timeActiveIdx = 255;
		}
		if (_timeLastValid != 0) {
			// Fallback: TimeLib тикает сам; setTime не трогаем, если она уже настроена
			// (не конфликтуем с sync-provider NTPClientLib).
			if (timeStatus() != timeSet) { setTime(_timeLastValid); }
		}
		// Аварийный timestamp продолжает идти: обновляем now и производные.
		core_state.signal("time.now", BusValue::tm((int64_t)now()));
		timeUpdateDerived();
	}

	// 9. Обратная синхронизация RTC (подчинённые источники).
	if (_timeSyncIntervalS > 0 && activeIdx >= 0) {
		uint32_t nowS = millis() / 1000UL;
		if (nowS - _timeLastRtcSync >= _timeSyncIntervalS) {
			_timeLastRtcSync = nowS;
			int32_t activePrio = _timeSrc[activeIdx].prio;
			time_t sysT = (time_t)now();
			for (uint8_t i = 0; i < _timeSrcCount; i++) {
				if (!_timeSrc[i].enabled) { continue; }
				if (_timeSrc[i].set == nullptr) { continue; }
				if (_timeSrc[i].prio >= activePrio) { continue; }
				_timeSrc[i].set(sysT);
			}
		}
	}

	// 10. Реконсиляция tz/dst (запись через шину применяется здесь).
	int32_t busTz = core_state.getInt("time.tz", _timeTzDec);
	bool    busDst = core_state.getBool("time.dst", _timeDst);
	if (busTz != _timeTzDec || busDst != _timeDst) {
		_timeTzDec = busTz;
		_timeDst   = busDst;
		core_state.signal("time.tz",  BusValue::i32(_timeTzDec));
		core_state.signal("time.dst", BusValue::bo(_timeDst));
		core_state.emit("time.tz_changed");
	}
}

void CLASS_CORE_SYS::timeUpdateDerived() {
	time_t t = (time_t)now();
	char buf[24];
	ns_core_sys::timeFormatNowStr(t, buf, sizeof(buf));
	core_state.signal("time.now_str", BusValue::str(String(buf)));
	core_state.signal("time.hour",    BusValue::i32(hour(t)));
	core_state.signal("time.minute",  BusValue::i32(minute(t)));
	core_state.signal("time.second",  BusValue::i32(second(t)));
}

// ============================================================
// Геттеры
// ============================================================

time_t CLASS_CORE_SYS::timeNow() const {
	return (time_t)now();
}

bool CLASS_CORE_SYS::timeValid() const {
	return (_timeActiveIdx != 255);
}

const char* CLASS_CORE_SYS::timeSourceName() const {
	if (_timeActiveIdx == 255 || _timeActiveIdx >= _timeSrcCount) { return ""; }
	return _timeSrc[_timeActiveIdx].name;
}

int8_t CLASS_CORE_SYS::timeZoneHours() const {
	return (int8_t)(_timeTzDec / 10);
}

int8_t CLASS_CORE_SYS::timeZoneMinutes() const {
	return (int8_t)((_timeTzDec % 10) * 6);
}

bool CLASS_CORE_SYS::daylight() const {
	return _timeDst;
}

// ============================================================
// Bus-функции
// ============================================================

void CLASS_CORE_SYS::applyTimeSet(time_t v) {
	if (v <= 0) { return; }
	setTime(v);
	_timeLastValid   = v;
	_timeLastValidMs = millis();
	core_state.signal("time.now", BusValue::tm((int64_t)v));
	timeUpdateDerived();
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		if (_timeSrc[i].set != nullptr) { _timeSrc[i].set(v); }
	}
}

int CLASS_CORE_SYS::cmdTimeSet(void* user, int argc, const BusValue* argv, BusValue& result) {
	(void)result;
	CLASS_CORE_SYS* self = (CLASS_CORE_SYS*)user;
	if (self == nullptr) { return BUS_ERR_INTERNAL; }
	if (argc < 1 || argv == nullptr || argv[0].kind != BusValue::TIME) { return BUS_ERR_BAD_TYPE; }
	if (argv[0].t <= 0) { return BUS_ERR_BAD_VALUE; }
	self->applyTimeSet((time_t)argv[0].t);
	return BUS_OK;
}

int CLASS_CORE_SYS::cmdTimeSyncFrom(void* user, int argc, const BusValue* argv, BusValue& result) {
	(void)result;
	CLASS_CORE_SYS* self = (CLASS_CORE_SYS*)user;
	if (self == nullptr) { return BUS_ERR_INTERNAL; }
	if (argc < 1 || argv == nullptr || argv[0].kind != BusValue::STR) { return BUS_ERR_BAD_TYPE; }
	int idx = self->findTimeSource(argv[0].s.c_str());
	if (idx < 0) { return BUS_ERR_NOT_FOUND; }
	strTimeSource& src = self->_timeSrc[idx];
	if (timeSourceReason(src)[0] != 0) { return BUS_ERR_NOT_READY; }
	time_t t = 0;
	if (!src.get(t)) { return BUS_ERR_NOT_READY; }

	// Форсированный опрос: явное разрешение отката назад.
	src.validLast = true;
	src.lastOkMs = millis();
	setTime(t);
	self->_timeLastValid   = t;
	self->_timeLastValidMs = millis();
	bool switched = (self->_timeActiveIdx != (uint8_t)idx);
	self->_timeActiveIdx = (uint8_t)idx;
	core_state.signal("time.valid", BusValue::bo(true));
	core_state.signal("time.source", BusValue::str(src.name));
	core_state.signal("time.now", BusValue::tm((int64_t)t));
	self->timeUpdateDerived();
	if (switched) { core_state.emit("time.source_changed"); }
	return BUS_OK;
}

int CLASS_CORE_SYS::cmdTimeSave(void* user, int argc, const BusValue* argv, BusValue& result) {
	(void)argc; (void)argv; (void)result;
	CLASS_CORE_SYS* self = (CLASS_CORE_SYS*)user;
	if (self == nullptr) { return BUS_ERR_INTERNAL; }
	if (self->_timePendingSave) { return BUS_OK; }
	self->_timePendingSave = true;
	core_task.after("time.save", cbTimeSaveTask, 500);
	return BUS_OK;
}

void CLASS_CORE_SYS::cbTimeTick() {
	core_sys.timeTick();
}

void CLASS_CORE_SYS::cbTimeSaveTask() {
	if (!core_sys._timePendingSave) { return; }
	core_sys._timePendingSave = false;
	core_sys.save_config_Time();
}

// ============================================================
// Конфиг /config_time.json
// ============================================================

bool CLASS_CORE_SYS::load_config_Time() {
	_timeTzDec         = 0;
	_timeDst           = false;
	_timeSyncIntervalS = 3600;   // дефолт (0 = off)

	JsonDocument doc;
	if (core_json.jsonFileLoadDoc(CONFIG_FILE_TIME, doc)) {
		_timeTzDec = doc["timeZone"] | 0;
		_timeDst   = doc["daylight"] | false;
		uint32_t si = doc["syncIntervalS"] | 3600;
		_timeSyncIntervalS = (si > 86400UL) ? 86400UL : si;
		if (_timeTzDec < -120) { _timeTzDec = -120; }
		if (_timeTzDec > 130)  { _timeTzDec = 130; }
		return true;
	}

	// Миграция legacy из config_ntp.json (однократно, без автосейва).
	JsonDocument legacy;
	if (core_json.jsonFileLoadDoc(TIME_LEGACY_NTP_CFG, legacy)) {
		_timeTzDec = legacy["timeZone"] | 0;
		_timeDst   = legacy["daylight"] | false;
		if (_timeTzDec < -120) { _timeTzDec = -120; }
		if (_timeTzDec > 130)  { _timeTzDec = 130; }
		DEBUGSYS("time: migrated TZ/DST from %s\r\n", TIME_LEGACY_NTP_CFG);
	}
	return false;
}

bool CLASS_CORE_SYS::save_config_Time() {
	DEBUGSYS("Save config TIME\r\n");
	JsonDocument doc;
	core_json.jsonFileLoadDoc(CONFIG_FILE_TIME, doc);
	doc["timeZone"]      = _timeTzDec;
	doc["daylight"]      = _timeDst;
	doc["syncIntervalS"] = _timeSyncIntervalS;
	JsonObject sources = doc["sources"].to<JsonObject>();
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		JsonObject o = sources[_timeSrc[i].name].to<JsonObject>();
		o["prio"]    = _timeSrc[i].prio;
		o["enabled"] = _timeSrc[i].enabled;
	}
	return core_json.jsonFileSaveDoc(CONFIG_FILE_TIME, doc);
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_SYS::handleTimeInfo(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	time_t t = (time_t)now();
	char buf[24];
	ns_core_sys::timeFormatNowStr(t, buf, sizeof(buf));
	char ebuf[24];
	snprintf(ebuf, sizeof(ebuf), "%lld", (long long)t);

	String values = "";
	values += "time_now|"          + String(ebuf)        + "|div\n";
	values += "time_now_str|"      + String(buf)         + "|div\n";
	values += "time_valid|"        + String(timeValid() ? "1" : "0") + "|div\n";
	values += "time_valid_str|"    + String(timeValid() ? "valid" : "invalid") + "|div\n";
	values += "time_source|"       + String(timeSourceName()) + "|div\n";
	values += "time_source_count|" + String((int)_timeSrcCount) + "|div\n";
	String csv;
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		if (i > 0) { csv += ","; }
		csv += _timeSrc[i].name;
	}
	values += "time_sources|"      + csv + "|div\n";
	values += "time_tz|"           + String(_timeTzDec) + "|select\n";
	values += "time_dst|"          + String(_timeDst ? "checked" : "") + "|chk\n";
	values += "time_sync_interval|" + String(_timeSyncIntervalS) + "|input\n";
	request->send(200, "text/plain", values);
}

void CLASS_CORE_SYS::handleTimeSources(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	AsyncResponseStream *resp = request->beginResponseStream("application/json");
	resp->print("[");
	for (uint8_t i = 0; i < _timeSrcCount; i++) {
		if (i > 0) { resp->print(","); }
		uint32_t lastOkSec = (_timeSrc[i].lastOkMs == 0)
			? 0 : (millis() - _timeSrc[i].lastOkMs) / 1000UL;
		const char* why = timeSourceUiReason(_timeSrc[i]);
		resp->print("{\"name\":\"");
		resp->print(_timeSrc[i].name);
		resp->print("\",\"prio\":");
		resp->print(_timeSrc[i].prio);
		resp->print(",\"enabled\":");
		resp->print(_timeSrc[i].enabled ? "true" : "false");
		resp->print(",\"valid\":");
		resp->print(_timeSrc[i].validLast ? "true" : "false");
		resp->print(",\"lastOkSec\":");
		resp->print(lastOkSec);
		resp->print(",\"status\":\"");
		resp->print(why);
		resp->print("\"}");
	}
	resp->print("]");
	request->send(resp);
}

void CLASS_CORE_SYS::handleTimeSave(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	bool tzChanged = false;

	for (int i = 0; i < (int)request->args(); i++) {
		String name = request->argName(i);
		String val  = request->arg(i);

		if (name == "tz") {
			int32_t tz = 0;
			if (!ns_core_sys::timeParseTz(val, tz)) {
				request->send(400, "text/plain", "tz out of range");
				return;
			}
			if (tz != _timeTzDec) { _timeTzDec = tz; tzChanged = true; }
			continue;
		}
		if (name == "dst") {
			bool dst = timeParseBool(val);
			if (dst != _timeDst) { _timeDst = dst; tzChanged = true; }
			continue;
		}
		if (name == "syncIntervalS") {
			long si = val.toInt();
			if (si < 0 || si > 86400) {
				request->send(400, "text/plain", "syncIntervalS out of range");
				return;
			}
			_timeSyncIntervalS = (uint32_t)si;
			continue;
		}
		if (name.endsWith("_prio")) {
			String base = name.substring(0, name.length() - 5);
			int idx = findTimeSource(base.c_str());
			if (idx < 0) { continue; }
			long p = val.toInt();
			if (p < 0 || p > 1000) {
				request->send(400, "text/plain", "prio out of range");
				return;
			}
			_timeSrc[idx].prio = (int32_t)p;
			continue;
		}
		if (name.endsWith("_enabled")) {
			String base = name.substring(0, name.length() - 8);
			int idx = findTimeSource(base.c_str());
			if (idx < 0) { continue; }
			_timeSrc[idx].enabled = timeParseBool(val);
			continue;
		}
	}

	core_state.signal("time.tz",  BusValue::i32(_timeTzDec));
	core_state.signal("time.dst", BusValue::bo(_timeDst));
	if (tzChanged) { core_state.emit("time.tz_changed"); }

	save_config_Time();
	request->send(200, "text/plain", "OK");
}

void CLASS_CORE_SYS::handleTimeSet(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	if (!request->hasArg("value")) {
		request->send(400, "text/plain", "No value");
		return;
	}
	long long v = strtoll(request->arg("value").c_str(), nullptr, 10);
	if (v <= 0) {
		request->send(400, "text/plain", "Bad value");
		return;
	}
	applyTimeSet((time_t)v);
	request->send(200, "text/plain", "OK");
}

void CLASS_CORE_SYS::handleTimeSync(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	if (!request->hasArg("source")) {
		request->send(400, "text/plain", "No source");
		return;
	}
	BusValue arg = BusValue::str(request->arg("source"));
	BusValue result;
	int rc = cmdTimeSyncFrom(this, 1, &arg, result);
	if (rc == BUS_OK) {
		request->send(200, "text/plain", "OK");
	}
	else {
		request->send(200, "text/plain", String("ERR ") + String(rc));
	}
}

void CLASS_CORE_SYS::handleTimeVer(AsyncWebServerRequest *request) {
	DEBUGSYS("%s\r\n", __FUNCTION__);
	String values = "";
	values += "timeversion|" + String(CORE_SYS_VERSION)          + "|div\n";
	values += "timegentime|" + String(CORE_SYS_GENERATED_TIME)   + "|div\n";
	values += "timegendate|" + String(CORE_SYS_COMMIT_DATE_STR)  + "|div\n";
	request->send(200, "text/plain", values);
}
