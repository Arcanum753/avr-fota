#include <Arduino.h>
#include "version.h"
#include "main.h"

#include <WiFiClient.h>
#if defined(ESP32)
#include <Update.h>
#include <HTTPClient.h>
#elif defined(ESP8266)
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#endif
#include <core_ntp/NtpClientLib.h>
#include "core_json/core_json.h"
#include "module_otaclient/module_otaclient.h"
#include "eertos.h"
#include "common.h"
#include "module_otaclient_version.h"

// Single global object - the class itself
MODULE_CLASS_OTACLIENT otaClient; 

// Static manifest entries buffer (avoid stack allocation)
ManifestEntry MODULE_CLASS_OTACLIENT::_manifestEntries[OTACLIENT_MAX_MANIFEST_ENTRIES];

MODULE_CLASS_OTACLIENT::MODULE_CLASS_OTACLIENT() : CORE_OTA_CLASS(true) {
    _isStarted = false;
    _updateInProgress = false;
    _updateRetries = 0;
    _lastReportedPercent = 0;
    _testStatusCode = OTACLIENT_TEST_IDLE;
    _testCompareResult = 0;
    _testFwCompareResult = 0;
    _testFsCompareResult = 0;
    _fsEnded = false;
}

uint16_t MODULE_CLASS_OTACLIENT::getTimeOut()          { return _config.timeOut; }
bool MODULE_CLASS_OTACLIENT::powerOnGet()              { return _config.powerOn; }
String MODULE_CLASS_OTACLIENT::serverAddressGet()      { return _config.serverAddress; }
uint16_t MODULE_CLASS_OTACLIENT::serverPortGet()       { return _config.serverPort; }
String MODULE_CLASS_OTACLIENT::manifestPathGet()       { return _config.manifestPath; }
uint8_t MODULE_CLASS_OTACLIENT::isStart()              { return _isStarted; }

void MODULE_CLASS_OTACLIENT::begin(String _hostname, String _password) {
    CORE_OTA_CLASS::begin(_hostname, _password);
    defaultConfig();
    if ( load_config() == false) {save_config();}
    DEBUGOTACLIENT("%s\r\n", __FUNCTION__);
    if (_isStarted){    return;    }
    
    _isStarted = true;
    SetTimerTask(otaclientTimer, SEC * MINUTES * otaClient.getTimeOut());
    SetTimerTask(otaclientLoopTask, 50);
}

// ========== TIMER (for calling from other files) ==========
void otaclientTimer() {
    uint16_t timeout = otaClient.getTimeOut();
    if (otaClient.isStart() == false){   return; }
    if (timeout > 60){ timeout = 60;}
    if (timeout == 0) { return;  }
    
    // Don't start a new check if update is already in progress
    if (otaClient._updateInProgress) {
        DEBUGOTACLIENT("OtaClient update in progress, rescheduling timer\n");
        SetTimerTask(otaclientTimer, SEC * MINUTES * timeout);
        return;
    }
    
    DEBUGOTACLIENT("OtaClient timeout %d min. ", timeout);
    SetTimerTask(otaclientTimer, SEC * MINUTES * timeout);
    
    // Check for updates on each timer tick
    otaClient.checkForUpdates();
}

// ========== LOOP TASK (called via eertos timer every 50ms) ==========
void otaclientLoopTask() {
    otaClient.loop();
    SetTimerTask(otaclientLoopTask, 50);
}

// ========== ON WiFi CONNECT ==========
void MODULE_CLASS_OTACLIENT::onWiFiConnect() {
    DEBUGOTACLIENT("%s: powerOn=%d\r\n", __FUNCTION__, _config.powerOn);
    if (_config.powerOn) {
#if defined(ESP8266)
        // ESP8266: отложить проверку обновлений на 5 секунд после WiFi connect,
        // чтобы дать стеку WiFi стабилизироваться и избежать WDT reset
        SetTimerTask(otaclientTimer, SEC * 5);
#else
        checkForUpdates();
#endif
    }
}

// ========== TEST (async - just sets flag) ==========
void MODULE_CLASS_OTACLIENT::test(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    
    // Защита от множественных тестов: если тест уже выполняется, отклоняем
    if (_testStatusCode == OTACLIENT_TEST_PENDING) {
        DEBUGOTACLIENT("Test already in progress, rejecting\n");
        request->send(429, "text/plain", "Test already in progress");
        return;
    }
    
    _testStatusCode = OTACLIENT_TEST_PENDING;
    _testStatusMessage = "Checking...";
    _testUpdateName = "";
    _testUpdateUrl = "";
    _testUpdateSize = 0;
    _testUpdateMd5 = "";
    _testUpdateFileType = -1;
    _testCompareResult = 0;
    request->send(200, "text/plain", "OK");
}

// ========== LOOP (called from main loop) ==========
void MODULE_CLASS_OTACLIENT::loop() {
    // Don't process any loop actions while update is in progress
    if (_updateInProgress) {
        return;
    }
    
    // Handle pending test
    if (_testStatusCode == OTACLIENT_TEST_PENDING) {
        _testStatusCode = OTACLIENT_TEST_IDLE;
        
        // Check WiFi
        if (WiFi.status() != WL_CONNECTED) {
            _testStatusCode = OTACLIENT_TEST_SERVER_UNAVAIL;
            _testStatusMessage = "WiFi not connected";
            DEBUGOTACLIENT("Test: WiFi not connected\n");
            return;
        }
        
        // Fetch manifest (using static buffer to avoid stack allocation)
        int entryCount = 0;
        
        if (!fetchManifest(_manifestEntries, entryCount)) {
            _testStatusCode = OTACLIENT_TEST_SERVER_UNAVAIL;
            _testStatusMessage = "Server unavailable";
            DEBUGOTACLIENT("Test: server unavailable\n");
            return;
        }
        
        DEBUGOTACLIENT("Test: manifest has %d entries\n", entryCount);
        
        fileCompareResult fwResult, fsResult;
        bool fwValid, fsValid;
        _testCompareResult = 0;
        _testFwCompareResult = 0;
        _testFsCompareResult = 0;
        
        ManifestEntry* firmwareEntry = NULL;
        ManifestEntry* fsEntry = NULL;
        checkManifestEntries(_manifestEntries, entryCount, fwResult, fsResult, fwValid, fsValid,
                             _testFwCompareResult, _testFsCompareResult,
                             firmwareEntry, fsEntry);
        
        // Combine compare results for display:
        // - If any is NEWER (1) -> combined = 1
        // - Else if any is SAME (0) -> combined = 0
        // - Else if any is OLDER (-1) -> combined = -1
        // - Else combined = -2 (missing)
        if (_testFwCompareResult == 1 || _testFsCompareResult == 1) {
            _testCompareResult = 1;
        } else if (_testFwCompareResult == 0 || _testFsCompareResult == 0) {
            _testCompareResult = 0;
        } else if (_testFwCompareResult == -1 || _testFsCompareResult == -1) {
            _testCompareResult = -1;
        } else {
            _testCompareResult = -2;
        }
        
        // Build result message
        if (fwValid || fsValid) {
            _testStatusCode = OTACLIENT_TEST_UPDATE_AVAIL;
            _testStatusMessage = "Update available: ";
            
            if (fsValid && fsEntry) {
                _testStatusMessage += "FS: " + fsEntry->name;
                _testUpdateName = fsEntry->name;
                _testUpdateUrl = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + "/firmware/" + fsEntry->name;
                _testUpdateSize = fsEntry->size;
                _testUpdateMd5 = fsEntry->md5;
                _testUpdateFileType = FILE_TYPE_FILESYSTEM;
            }
            if (fwValid && firmwareEntry) {
                if (_testUpdateName.length() > 0) _testStatusMessage += ", ";
                _testStatusMessage += "FW: " + firmwareEntry->name;
                // Prefer firmware over FS for the update button
                _testUpdateName = firmwareEntry->name;
                _testUpdateUrl = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + "/firmware/" + firmwareEntry->name;
                _testUpdateSize = firmwareEntry->size;
                _testUpdateMd5 = firmwareEntry->md5;
                _testUpdateFileType = FILE_TYPE_FIRMWARE;
            }
        } else {
            _testStatusCode = OTACLIENT_TEST_NO_UPDATES;
            // Build detailed status message for both FW and FS
            if (firmwareEntry || fsEntry) {
                _testStatusMessage = "";
                if (firmwareEntry) {
                    _testStatusMessage += "FW: ";
                    if (_testFwCompareResult == 1) {
                        _testStatusMessage += "newer";
                    } else if (_testFwCompareResult == 0) {
                        _testStatusMessage += "current (same)";
                    } else if (_testFwCompareResult == -1) {
                        _testStatusMessage += "server older";
                    } else if (_testFwCompareResult == -2) {
                        _testStatusMessage += "no version info";
                    } else {
                        _testStatusMessage += "unknown";
                    }
                }
                if (fsEntry) {
                    if (_testStatusMessage.length() > 0) _testStatusMessage += ", ";
                    _testStatusMessage += "FS: ";
                    if (_testFsCompareResult == 1) {
                        _testStatusMessage += "newer";
                    } else if (_testFsCompareResult == 0) {
                        _testStatusMessage += "current (same)";
                    } else if (_testFsCompareResult == -1) {
                        _testStatusMessage += "server older";
                    } else if (_testFsCompareResult == -2) {
                        _testStatusMessage += "no version info";
                    } else {
                        _testStatusMessage += "unknown";
                    }
                }
                if (_testStatusMessage.length() == 0) {
                    _testStatusMessage = "No updates available";
                }
            } else {
                _testStatusMessage = "No updates available (no matching files)";
            }
        }
        
        DEBUGOTACLIENT("Test result: code=%d, fwCompare=%d, fsCompare=%d, combined=%d, msg=%s\n",
                       _testStatusCode, _testFwCompareResult, _testFsCompareResult, _testCompareResult, _testStatusMessage.c_str());
    }
    
    // Handle pending update (triggered by "Update Now" button)
    if (_testStatusCode == OTACLIENT_TEST_UPDATING) {
        _testStatusCode = OTACLIENT_TEST_IDLE;
        
        if (_testUpdateName.length() > 0 && _testUpdateUrl.length() > 0) {
            DEBUGOTACLIENT("Loop: starting update for %s\n", _testUpdateName.c_str());
            bool ok = downloadAndUpdate(_testUpdateUrl, _testUpdateSize, _testUpdateMd5, _testUpdateFileType);
            if (ok) {
                DEBUGOTACLIENT("Update successful, restarting...\n");
                delay(500);
                ESPHTTPServer.restart_esp();
            } else {
                _testStatusCode = OTACLIENT_TEST_SERVER_UNAVAIL;
                _testStatusMessage = "Update failed";
                DEBUGOTACLIENT("Update failed\n");
            }
        }
    }
}

// ========== WEB INIT ==========
void MODULE_CLASS_OTACLIENT::webInit(void) {
    registerCommonRoutes();
    registerCustomRoutes();
}

void MODULE_CLASS_OTACLIENT::registerCustomRoutes() {
    ESPHTTPServer.on(HTML_FILE_OTACLIENT, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        get_configuration_html(request);
    });
    
    ESPHTTPServer.on("/otaclient/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        send_configuration_values_html(request);
    });
    
    ESPHTTPServer.on("/otaclient/test", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        test(request);
    });
    
    // Test status endpoint (polled by JS)
    ESPHTTPServer.on("/otaclient/teststatus", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        String json = "{";
        json += "\"code\":" + String(_testStatusCode) + ",";
        json += "\"message\":\"" + _testStatusMessage + "\",";
        json += "\"name\":\"" + _testUpdateName + "\",";
        json += "\"url\":\"" + _testUpdateUrl + "\",";
        json += "\"size\":" + String(_testUpdateSize) + ",";
        json += "\"md5\":\"" + _testUpdateMd5 + "\",";
        json += "\"compare\":" + String(_testCompareResult);
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // Update trigger endpoint
    ESPHTTPServer.on("/otaclient/update", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        if (_testStatusCode == OTACLIENT_TEST_UPDATE_AVAIL && _testUpdateName.length() > 0) {
            _testStatusCode = OTACLIENT_TEST_UPDATING;
            _testStatusMessage = "Updating...";
            request->send(200, "text/plain", "Update started");
            // Run update asynchronously via loop()
        } else {
            request->send(400, "text/plain", "No update available");
        }
    });

    ESPHTTPServer.on("/otaclient/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

// ========== SEND CONFIG HTML ==========
void MODULE_CLASS_OTACLIENT::send_configuration_values_html(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaclienttime|"     + String(_config.timeOut) + "|input\n";
    values += "otaclientpoweron|"  + String(_config.powerOn ? "checked" : "") + "|chk\n";
    values += "otaclientaddr|"  + _config.serverAddress + "|input\n";
    values += "otaclientport|"  + String(_config.serverPort) + "|input\n";
    values += "otaclientmanifest|" + _config.manifestPath + "|input\n";
    request->send(200, "text/plain", values);
}

// ========== GET CONFIG HTML ==========
void MODULE_CLASS_OTACLIENT::get_configuration_html(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    _config.powerOn  = false; 
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGOTACLIENT("Arg %d: %s %s\r\n", i, 
                     request->argName(i).c_str(), 
                     request->arg(i).c_str());
            
            if (request->argName(i) == "otaclienttime")     { _config.timeOut = request->arg(i).toInt(); }
            if (request->argName(i) == "otaclientpoweron")  { _config.powerOn = true;  }
            if (request->argName(i) == "otaclientaddr")     { _config.serverAddress = urldecode(request->arg(i)); }
            if (request->argName(i) == "otaclientport")     { _config.serverPort = request->arg(i).toInt(); }
            if (request->argName(i) == "otaclientmanifest") { _config.manifestPath = urldecode(request->arg(i)); }
        }
        
        save_config();
        request->send(200, "application/json", "{\"success\":true}");
    }
    else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ========== JSON GET ==========
String MODULE_CLASS_OTACLIENT::jsonGet() {
    String ret = "";
    ret += "{\n";
    ret += "  \"deviceName\": \"" + ESPHTTPServer._sysConfig.deviceName + "\",\n";
    ret += "  \"deviceSerial\": \"" + ESPHTTPServer._sysConfig.deviceSerial + "\",\n";
    ret += "  \"ip\": \"" + WiFi.localIP().toString() + "\",\n";
    ret += "  \"mac\": \"" + WiFi.macAddress() + "\",\n";
    ret += "  \"timeOut\": " + String(_config.timeOut) + ",\n";
    ret += "  \"serverPort\": " + String(_config.serverPort) + ",\n";
    ret += "  \"target\": \"" + String(BUILD_ENV) + "\",\n";
    ret += "  \"buildtime\": \"" + String(BUILD_TIME) + "\",\n";
    ret += "  \"gitbranch\": \"" + String(GIT_BRANCH) + "\",\n";
    ret += "  \"gitcommit\": \"" + String(GIT_COMMIT) + "\",\n";
    ret += "  \"uptime\": \"" + String(NTP.getUptimeString()) + "\",\n";
    ret += "  \"rstreason\": \"" + ESPHTTPServer.getResetReason() + "\",\n";
    ret += "  \"espVer\": \"" + String(FIRMWARE_VERSION) + "\"\n";
    ret += "}\n";
    return ret;
}

// ========== DEFAULT CONFIG ==========
void MODULE_CLASS_OTACLIENT::defaultConfig() {
    _config.timeOut = OTACLIENT_TIME_DFLT;
    _config.powerOn = OTACLIENT_POWERON;
    _config.serverAddress = OTACLIENT_SERVER_ADDR;
    _config.serverPort = OTACLIENT_SERVER_PORT;
    _config.manifestPath = OTACLIENT_MANIFEST_PATH;
}

// ========== SAVE CONFIG ==========
bool MODULE_CLASS_OTACLIENT::save_config() {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_OTACLIENT, doc);
    doc["timeOut"] = _config.timeOut;
    doc["powerOn"] = _config.powerOn;
    doc["serverAddress"] = _config.serverAddress;
    doc["serverPort"] = _config.serverPort;
    doc["manifestPath"] = _config.manifestPath;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_OTACLIENT, doc);
}

// ========== LOAD CONFIG ==========
bool MODULE_CLASS_OTACLIENT::load_config() {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument doc;
    if (!ModClassJson.jsonFileLoadDoc(CONFIG_FILE_OTACLIENT, doc)) return false;
    _config.timeOut = doc["timeOut"].as<uint16_t>();
    _config.powerOn = doc["powerOn"].as<bool>();
    _config.serverAddress = doc["serverAddress"].as<String>();
    _config.serverPort = doc["serverPort"].as<uint16_t>();
    _config.manifestPath = doc["manifestPath"].as<String>();
    
    DEBUGOTACLIENT("timeOut: %d\n\r", _config.timeOut);
    DEBUGOTACLIENT("powerOn: %d\n\r", _config.powerOn);
    DEBUGOTACLIENT("serverAddress: %s\n\r", _config.serverAddress.c_str());
    DEBUGOTACLIENT("serverPort: %d\n\r", _config.serverPort);
    DEBUGOTACLIENT("manifestPath: %s\n\r", _config.manifestPath.c_str());
    
    return true;
}

// ============================================================
// FETCH MANIFEST from OTA server
// ============================================================
bool MODULE_CLASS_OTACLIENT::fetchManifest(ManifestEntry* entries, int& count) {
    count = 0;
    
    if (_config.serverAddress.length() == 0) {
        DEBUGOTACLIENT("fetchManifest: serverAddress is empty\n");
        return false;
    }
    
#if defined(ESP8266)
    // ESP8266: используем прямой WiFiClient с ручным WDT feed
    // чтобы избежать Soft WDT reset при длительных HTTP-запросах
    if (ESP.getFreeHeap() < 10000) {
        DEBUGOTACLIENT("fetchManifest: low memory (%d bytes), skipping\n", ESP.getFreeHeap());
        return false;
    }
#endif
    
    String url = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + _config.manifestPath + "?target=" + BUILD_ENV + "&ver=" + FIRMWARE_VERSION;
    DEBUGOTACLIENT("fetchManifest: %s\n", url.c_str());
    
#if defined(ESP8266)
    // Прямое подключение WiFiClient с ручным чтением и WDT feed
    WiFiClient client;
    client.setTimeout(5000);
    
    if (!client.connect(_config.serverAddress.c_str(), _config.serverPort)) {
        DEBUGOTACLIENT("fetchManifest: connect failed\n");
        return false;
    }
    
    // Отправляем HTTP GET запрос вручную
    client.print(String("GET ") + _config.manifestPath + " HTTP/1.1\r\n" +
                 "Host: " + _config.serverAddress + ":" + String(_config.serverPort) + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    // Читаем ответ с WDT feed
    // Используем поблочное чтение вместо построчного, чтобы корректно
    // обрабатывать chunked transfer encoding и длинные строки JSON
    unsigned long timeout = millis() + 5000;
    String payload = "";
    bool headersEnded = false;
    
    while (millis() < timeout) {
        if (client.available()) {
            if (!headersEnded) {
                String line = client.readStringUntil('\n');
                line.trim();
                if (line == "") {
                    headersEnded = true;
                }
                continue;
            }
            
            // Поблочное чтение тела ответа
            while (client.available()) {
                int c = client.read();
                if (c == -1) break;
                payload += (char)c;
                if (payload.length() > 2048) {  // Жёсткий лимит: 2048 байт
                    DEBUGOTACLIENT("fetchManifest: payload too large\n");
                    client.stop();
                    return false;
                }
            }
        } else {
            if (headersEnded && !client.connected()) {
                break;
            }
            delay(1);
            ESP.wdtFeed();
        }
    }
    
    client.stop();
    
    if (!headersEnded || payload.length() == 0) {
        DEBUGOTACLIENT("fetchManifest: timeout or empty response\n");
        return false;
    }
    
    DEBUGOTACLIENT("fetchManifest: received %d bytes\n", payload.length());
#else
    // ESP32: используем HTTPClient (более производительный)
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(10000);
    
    if (!http.begin(client, url)) {
        DEBUGOTACLIENT("fetchManifest: http.begin failed\n");
        return false;
    }
    
    int httpCode = http.GET();
    if (httpCode != 200) {
        DEBUGOTACLIENT("fetchManifest: HTTP error %d\n", httpCode);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    if (payload.length() > 2048) {  // Жёсткий лимит: 2048 байт для всех клиентов
        DEBUGOTACLIENT("fetchManifest: payload too large (%d bytes)\n", payload.length());
        return false;
    }
    
    DEBUGOTACLIENT("fetchManifest: received %d bytes\n", payload.length());
#endif
    
    // Parse JSON
    bool hasFiles = false;
    if (!ModClassJson.jsonParseNestedBool(payload, "has_files", hasFiles) || !hasFiles) {
        DEBUGOTACLIENT("fetchManifest: no files for target %s on server\n", BUILD_ENV);
        return false;
    }
    
    int numFiles = ModClassJson.jsonGetArraySize(payload, "files");
    if (numFiles <= 0) {
        DEBUGOTACLIENT("fetchManifest: no 'files' array in manifest\n");
        return false;
    }
    
    int idx = 0;
    for (int i = 0; i < numFiles && idx < OTACLIENT_MAX_MANIFEST_ENTRIES; i++) {
        String path = "files";
        ModClassJson.jsonGetArrayStr(payload, "files", i, "name", entries[idx].name);
        ModClassJson.jsonGetArrayStr(payload, "files", i, "type", entries[idx].type);
        int32_t sizeVal = 0;
        if (ModClassJson.jsonGetArrayInt(payload, "files", i, "size", sizeVal)) entries[idx].size = (size_t)sizeVal;
        ModClassJson.jsonGetArrayStr(payload, "files", i, "md5", entries[idx].md5);
        
        DEBUGOTACLIENT("  [%d] %s (%s) %u bytes MD5:%s\n",
            idx, entries[idx].name.c_str(), entries[idx].type.c_str(),
            entries[idx].size, entries[idx].md5.c_str());
        
        idx++;
    }
    
    count = idx;
    return count > 0;
}

// ============================================================
// UNIFIED MANIFEST ENTRY CHECKING
// ============================================================
void MODULE_CLASS_OTACLIENT::checkManifestEntries(ManifestEntry* entries, int count,
                                                    fileCompareResult& fwResult, fileCompareResult& fsResult,
                                                    bool& fwValid, bool& fsValid,
                                                    int8_t& fwCompareResult, int8_t& fsCompareResult,
                                                    ManifestEntry*& fwEntryOut, ManifestEntry*& fsEntryOut) {
    ManifestEntry* firmwareEntry = NULL;
    ManifestEntry* fsEntry = NULL;
    
    for (int i = 0; i < count; i++) {
        if (entries[i].type == "filesystem") {
            fsEntry = &entries[i];
            DEBUGOTACLIENT("  Found FS file: %s\n", entries[i].name.c_str());
        } else if (entries[i].type == "firmware") {
            firmwareEntry = &entries[i];
            DEBUGOTACLIENT("  Found firmware file: %s\n", entries[i].name.c_str());
        }
    }
    
    fwValid = false;
    fsValid = false;
    fwCompareResult = 0;
    fsCompareResult = 0;
    
    if (firmwareEntry) {
        int8_t ret = fileNameCheck(firmwareEntry->name, &fwResult);
        int8_t fwCompare = compareWithCurrentFsVersion(&fwResult, firmwareEntry->name);
        fwValid = (ret == 1 && fwCompare == 1);
        fwCompareResult = fwCompare;
        DEBUGOTACLIENT("Firmware version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                       fwResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fwResult.nameMatch, fwCompare, fwValid);
    }
    
    if (fsEntry) {
        int8_t ret = fileNameCheck(fsEntry->name, &fsResult);
        int8_t fsCompare = compareWithCurrentFsVersion(&fsResult, fsEntry->name);
        fsValid = (ret == 1 && fsCompare == 1);
        fsCompareResult = fsCompare;
        
        DEBUGOTACLIENT("FS version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                       fsResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fsResult.nameMatch, fsCompare, fsValid);
    }
    
    fwEntryOut = firmwareEntry;
    fsEntryOut = fsEntry;
}

// ============================================================
// PERFORM UPDATE FROM STREAM
// ============================================================
bool MODULE_CLASS_OTACLIENT::performUpdateFromStream(WiFiClient& stream, size_t size, const String& expectedMd5, int fileType) {
    DEBUGOTACLIENT("performUpdateFromStream: size=%u, type=%d, md5=%s\n", size, fileType, expectedMd5.c_str());
    
    // Set MD5 for verification
    if (expectedMd5.length() > 0) {
        Update.setMD5(expectedMd5.c_str());
    }
    
    // Determine partition
    int updatePartition = U_FLASH;
#if defined(ESP32)
    if (fileType == FILE_TYPE_FILESYSTEM) { updatePartition = U_SPIFFS; }
#elif defined(ESP8266)
    if (fileType == FILE_TYPE_FILESYSTEM) { updatePartition = U_FS; }
#endif
    
    // End filesystem before update
    fsEnd();
    _fsEnded = true;
    
#if defined(ESP8266)
    Update.runAsync(true);
#endif
    
    if (!Update.begin(size, updatePartition)) {
        DEBUGOTACLIENT("Update.begin failed!\n");
#ifdef DEBUG_OTA
        Update.printError(Serial);
#endif
        // Remount filesystem
        fsRemount();
        _fsEnded = false;
        return false;
    }
    
    // Stream data in chunks
    uint8_t buf[OTACLIENT_CHUNK_SIZE];
    size_t totalWritten = 0;
    size_t remaining = size;
    
    while (remaining > 0) {
        size_t toRead = (remaining < OTACLIENT_CHUNK_SIZE) ? remaining : OTACLIENT_CHUNK_SIZE;
        int bytesRead = stream.readBytes(buf, toRead);
        
#if defined(ESP8266)
        ESP.wdtFeed();
#endif
        
        if (bytesRead <= 0) {
            DEBUGOTACLIENT("Stream read error at %u/%u\n", totalWritten, size);
#if defined(ESP32)
            Update.abort();
#endif
#if defined(ESP8266)
            Update.end();
#endif
            fsRemount();
            _fsEnded = false;
            return false;
        }
        
        size_t written = Update.write(buf, bytesRead);
        if (written != (size_t)bytesRead) {
            DEBUGOTACLIENT("Update.write error: wrote %u of %u\n", written, bytesRead);
#if defined(ESP32)
            Update.abort();
#endif
#if defined(ESP8266)
            Update.end();
#endif
            fsRemount();
            _fsEnded = false;
            return false;
        }
        
        totalWritten += written;
        remaining -= bytesRead;
        
        // Progress (every 5%, no duplicates)
        uint16_t percent = (totalWritten * 100) / size;
        if ((percent % 5) == 0 && (percent != _lastReportedPercent)) {
            _lastReportedPercent = percent;
            DEBUGOTACLIENT("Update progress: %u%%\n", percent);
        }
    }
    
    // Finalize update
    if (!Update.end(true)) {
        DEBUGOTACLIENT("Update.end failed!\n");
#ifdef DEBUG_OTA
        Update.printError(Serial);
#endif
        fsRemount();
        _fsEnded = false;
        return false;
    }
    
    DEBUGOTACLIENT("Update successful! MD5: %s\n", Update.md5String().c_str());
    
    // Update was successful, FS will be remounted after restart
    _fsEnded = true;
    
    return true;
}

// ============================================================
// DOWNLOAD AND UPDATE a single file
// ============================================================
bool MODULE_CLASS_OTACLIENT::downloadAndUpdate(const String& url, size_t size, const String& md5, int fileType) {
    DEBUGOTACLIENT("downloadAndUpdate: %s (%u bytes)\n", url.c_str(), size);
    
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(30000); // 30 second timeout for download
    
    if (!http.begin(client, url)) {
        DEBUGOTACLIENT("downloadAndUpdate: http.begin failed\n");
        return false;
    }
    
    int httpCode = http.GET();
    if (httpCode != 200) {
        DEBUGOTACLIENT("downloadAndUpdate: HTTP error %d\n", httpCode);
        http.end();
        return false;
    }
    
    // Verify content length matches expected size
    int contentLength = http.getSize();
    if (contentLength > 0 && (size_t)contentLength != size) {
        DEBUGOTACLIENT("downloadAndUpdate: size mismatch! expected %u, got %d\n", size, contentLength);
        http.end();
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    bool success = performUpdateFromStream(*stream, size, md5, fileType);
    
    http.end();
    return success;
}

// ============================================================
// CHECK FOR UPDATES (main logic)
// ============================================================
void MODULE_CLASS_OTACLIENT::checkForUpdates() {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    
    // Don't start if already updating
    if (_updateInProgress) {
        DEBUGOTACLIENT("Update already in progress, skipping\n");
        return;
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        DEBUGOTACLIENT("WiFi not connected, skipping\n");
        return;
    }
    
    // Fetch manifest (using static buffer to avoid stack allocation)
    int entryCount = 0;
    
    if (!fetchManifest(_manifestEntries, entryCount)) {
        DEBUGOTACLIENT("Failed to fetch manifest\n");
        return;
    }
    
    DEBUGOTACLIENT("Manifest has %d entries\n", entryCount);
    
    fileCompareResult fwResult, fsResult;
    bool fwValid, fsValid;
    int8_t fwCompareResult, fsCompareResult;
    ManifestEntry* firmwareEntry = NULL;
    ManifestEntry* fsEntry = NULL;
    
    checkManifestEntries(_manifestEntries, entryCount, fwResult, fsResult, fwValid, fsValid,
                         fwCompareResult, fsCompareResult,
                         firmwareEntry, fsEntry);
    
    // Decision logic:
    // If both are valid and have the same version -> update FS first, then firmware
    // If only one is valid -> update that one
    // If both valid but different versions -> update each independently (FS first)
    
    _updateInProgress = true;
    
    // Stop the loop task timer to prevent re-entrancy during update
    DelTimerTask(otaclientLoopTask);
    
    // First, update filesystem if valid
    if (fsValid && fsEntry) {
        String url = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + "/firmware/" + fsEntry->name;
        DEBUGOTACLIENT("Updating FS from %s\n", url.c_str());
        
        bool fsOk = downloadAndUpdate(url, fsEntry->size, fsEntry->md5, FILE_TYPE_FILESYSTEM);
        if (fsOk) {
            DEBUGOTACLIENT("FS update successful, restarting...\n");
            delay(500);
            ESPHTTPServer.restart_esp();
            return;
        } else {
            DEBUGOTACLIENT("FS update failed\n");
        }
    }
    
    // Then, update firmware if valid
    if (fwValid && firmwareEntry) {
        String url = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + "/firmware/" + firmwareEntry->name;
        DEBUGOTACLIENT("Updating firmware from %s\n", url.c_str());
        
        bool fwOk = downloadAndUpdate(url, firmwareEntry->size, firmwareEntry->md5, FILE_TYPE_FIRMWARE);
        if (fwOk) {
            DEBUGOTACLIENT("Firmware update successful, restarting...\n");
            delay(500);
            ESPHTTPServer.restart_esp();
            // Never reaches here after restart
            return;
        } else {
            DEBUGOTACLIENT("Firmware update failed\n");
        }
    }
    
    _updateInProgress = false;
    
    // Re-enable loop task timer after update completes (or fails)
    SetTimerTask(otaclientLoopTask, 50);
    
    // If FS was ended during update attempt, try to remount it
    if (_fsEnded) {
        fsRemount();
        _fsEnded = false;
    }
    
    if (!fwValid && !fsValid) {
        DEBUGOTACLIENT("No valid updates found (server version not newer than current)\n");
    }
}

String MODULE_CLASS_OTACLIENT::getVersionStr(){
    return String(MODULE_OTACLIENT_VERSION);
}

String MODULE_CLASS_OTACLIENT::getGeneratedTime(){
    return String(MODULE_OTACLIENT_GENERATED_TIME);
}

String MODULE_CLASS_OTACLIENT::getCommitDateStr(){
    return String(MODULE_OTACLIENT_COMMIT_DATE_STR);
}

void MODULE_CLASS_OTACLIENT::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGOTACLIENT("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaclientversion|"     + getVersionStr()    + "|dev\n";
    values += "otaclientgentime|"     + getGeneratedTime() + "|dev\n";
    values += "otaclientgendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}

