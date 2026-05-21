#include <Arduino.h>
#include <ArduinoJson.h>
#include "version.h"
#include "main.h"

#include <WiFiClient.h>
#include <Update.h>
#if defined(ESP32)
#include <HTTPClient.h>
#elif defined(ESP8266)
#include <ESP8266HTTPClient.h>
#endif
#include <core_ntp/NtpClientLib.h>
#include "core_json/core_json.h"
#include "module_otaclient/module_otaclient.h"
#include "core_ota/core_ota.h"
#include "eertos.h"
#include "common.h"
#include "module_otaclient_version.h"

// Single global object - the class itself
MODULE_CLASS_OTACLIENT otaClient;

// Static manifest entries buffer (avoid stack allocation)
ManifestEntry MODULE_CLASS_OTACLIENT::_manifestEntries[OTACLIENT_MAX_MANIFEST_ENTRIES];

MODULE_CLASS_OTACLIENT::MODULE_CLASS_OTACLIENT() {
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

void MODULE_CLASS_OTACLIENT::begin() {
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
        checkForUpdates();
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
        
        // Find our files (matching BUILD_ENV)
        ManifestEntry* firmwareEntry = NULL;
        ManifestEntry* fsEntry = NULL;
        
        for (int i = 0; i < entryCount; i++) {
            if (!_manifestEntries[i].name.startsWith(BUILD_ENV)) {
                DEBUGOTACLIENT("  Skipping %s (wrong device)\n", _manifestEntries[i].name.c_str());
                continue;
            }
            
            if (_manifestEntries[i].type == "filesystem") {
                fsEntry = &_manifestEntries[i];
                DEBUGOTACLIENT("  Found FS file: %s\n", _manifestEntries[i].name.c_str());
            } else if (_manifestEntries[i].type == "firmware") {
                firmwareEntry = &_manifestEntries[i];
                DEBUGOTACLIENT("  Found firmware file: %s\n", _manifestEntries[i].name.c_str());
            }
        }
        
        // Check versions using core_ota's fileNameCheck + compareWithCurrentFsVersion
        fileCompareResult fwResult, fsResult;
        bool fwValid = false;
        bool fsValid = false;
        _testCompareResult = 0;     // default: same/missing
        _testFwCompareResult = 0;   // default: same
        _testFsCompareResult = 0;   // default: same
        
        if (firmwareEntry) {
            int8_t ret = modOtaClass.fileNameCheck(firmwareEntry->name, &fwResult);
            // Use compareWithCurrentFsVersion to check if server version is NEWER
            int8_t fwCompare = modOtaClass.compareWithCurrentFsVersion(&fwResult, firmwareEntry->name);
            fwValid = (ret == 1 && fwCompare == 1);  // only valid if truly NEWER
            _testFwCompareResult = fwCompare;
            DEBUGOTACLIENT("Firmware version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                           fwResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fwResult.nameMatch, fwCompare, fwValid);
        }
        
        if (fsEntry) {
            int8_t ret = modOtaClass.fileNameCheck(fsEntry->name, &fsResult);
            // Use compareWithCurrentFsVersion to check if server version is NEWER
            int8_t fsCompare = modOtaClass.compareWithCurrentFsVersion(&fsResult, fsEntry->name);
            fsValid = (ret == 1 && fsCompare == 1);  // only valid if truly NEWER
            _testFsCompareResult = fsCompare;
            DEBUGOTACLIENT("FS version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                           fsResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fsResult.nameMatch, fsCompare, fsValid);
        }
        
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
        
        request->send_P(200, "text/html", Page_GeneralOtaClient);
        save_config();
        otaclientTimer();
    }
    else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ========== JSON GET ==========
String MODULE_CLASS_OTACLIENT::jsonGet() {
    String ret = "";
    JsonDocument jsonDoc;
    
    jsonDoc["deviceName"]   = ESPHTTPServer._sysConfig.deviceName;
    jsonDoc["deviceSerial"] = ESPHTTPServer._sysConfig.deviceSerial;
    
    jsonDoc["ip"]           = WiFi.localIP().toString();
    jsonDoc["mac"]          = WiFi.macAddress();
    jsonDoc["timeOut"]   = _config.timeOut;
    jsonDoc["serverPort"] = _config.serverPort;

    jsonDoc["target"]       = BUILD_ENV;
    jsonDoc["buildtime"]    = BUILD_TIME;
    jsonDoc["gitbranch"]    = GIT_BRANCH;
    jsonDoc["gitcommit"]    = GIT_COMMIT;
    jsonDoc["uptime"]       = (String)NTP.getUptimeString();
    jsonDoc["rstreason"]    =  ESPHTTPServer.getResetReason();

    jsonDoc["espVer"]       = FIRMWARE_VERSION;
    jsonDoc["webVer"]       = VERSION_WEB;
	
	serializeJsonPretty(jsonDoc, ret);
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
    JsonDocument jsonDoc;
    jsonDoc["timeOut"]        = _config.timeOut;
    jsonDoc["powerOn"]        = _config.powerOn;
    jsonDoc["serverAddress"]  = _config.serverAddress;
    jsonDoc["serverPort"]     = _config.serverPort;
    jsonDoc["manifestPath"]   = _config.manifestPath;
    return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_FILE_OTACLIENT);
}

// ========== LOAD CONFIG ==========
bool MODULE_CLASS_OTACLIENT::load_config() {
    DEBUGOTACLIENT("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument jsonDoc;
    if (ModClassJson.load_jsonDoc(CONFIG_FILE_OTACLIENT, jsonDoc) == false) { return false; }
    
    _config.timeOut        = jsonDoc["timeOut"].as<int>();
    _config.powerOn        = jsonDoc["powerOn"].as<bool>();
    _config.serverAddress  = jsonDoc["serverAddress"].as<const char *>();
    _config.serverPort     = jsonDoc["serverPort"].as<uint16_t>();
    _config.manifestPath   = jsonDoc["manifestPath"].as<const char *>();
    
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
    
    String url = "http://" + _config.serverAddress + ":" + String(_config.serverPort) + _config.manifestPath;
    DEBUGOTACLIENT("fetchManifest: %s\n", url.c_str());
    
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(10000); // 10 second timeout
    
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
    
    DEBUGOTACLIENT("fetchManifest: received %d bytes\n", payload.length());
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        DEBUGOTACLIENT("fetchManifest: JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    JsonArray files = doc["files"].as<JsonArray>();
    if (files.isNull()) {
        DEBUGOTACLIENT("fetchManifest: no 'files' array in manifest\n");
        return false;
    }
    
    int idx = 0;
    for (JsonObject file : files) {
        if (idx >= OTACLIENT_MAX_MANIFEST_ENTRIES) break;
        
        entries[idx].name = file["name"].as<const char *>();
        entries[idx].type = file["type"].as<const char *>();
        entries[idx].size = file["size"].as<size_t>();
        entries[idx].md5  = file["md5"].as<const char *>();
        
        DEBUGOTACLIENT("  [%d] %s (%s) %u bytes MD5:%s\n",
            idx, entries[idx].name.c_str(), entries[idx].type.c_str(),
            entries[idx].size, entries[idx].md5.c_str());
        
        idx++;
    }
    
    count = idx;
    return count > 0;
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
    if (modOtaClass._fs) {
        DEBUGOTACLIENT("Ending filesystem...\n");
        modOtaClass._fs->end();
        _fsEnded = true;
        delay(100);
    }
    
#if defined(ESP8266)
    Update.runAsync(true);
#endif
    
    if (!Update.begin(size, updatePartition)) {
        DEBUGOTACLIENT("Update.begin failed!\n");
#ifdef DEBUG_OTA
        Update.printError(Serial);
#endif
        // Remount filesystem
        if (modOtaClass._fs) {
#if defined(ESP32)
            modOtaClass._fs->begin(true);
#elif defined(ESP8266)
            modOtaClass._fs->begin();
#endif
            _fsEnded = false;
        }
        return false;
    }
    
    // Stream data in chunks
    uint8_t buf[OTACLIENT_CHUNK_SIZE];
    size_t totalWritten = 0;
    size_t remaining = size;
    
    while (remaining > 0) {
        size_t toRead = (remaining < OTACLIENT_CHUNK_SIZE) ? remaining : OTACLIENT_CHUNK_SIZE;
        int bytesRead = stream.readBytes(buf, toRead);
        
        if (bytesRead <= 0) {
            DEBUGOTACLIENT("Stream read error at %u/%u\n", totalWritten, size);
            Update.abort();
            if (modOtaClass._fs) {
#if defined(ESP32)
                modOtaClass._fs->begin(true);
#elif defined(ESP8266)
                modOtaClass._fs->begin();
#endif
                _fsEnded = false;
            }
            return false;
        }
        
        size_t written = Update.write(buf, bytesRead);
        if (written != (size_t)bytesRead) {
            DEBUGOTACLIENT("Update.write error: wrote %u of %u\n", written, bytesRead);
            Update.abort();
            if (modOtaClass._fs) {
#if defined(ESP32)
                modOtaClass._fs->begin(true);
#elif defined(ESP8266)
                modOtaClass._fs->begin();
#endif
                _fsEnded = false;
            }
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
        if (modOtaClass._fs) {
#if defined(ESP32)
            modOtaClass._fs->begin(true);
#elif defined(ESP8266)
            modOtaClass._fs->begin();
#endif
            _fsEnded = false;
        }
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
    
    // Find our files (matching BUILD_ENV)
    ManifestEntry* firmwareEntry = NULL;
    ManifestEntry* fsEntry = NULL;
    
    for (int i = 0; i < entryCount; i++) {
        // Check if filename starts with our build environment
        if (!_manifestEntries[i].name.startsWith(BUILD_ENV)) {
            DEBUGOTACLIENT("  Skipping %s (wrong device)\n", _manifestEntries[i].name.c_str());
            continue;
        }
        
        if (_manifestEntries[i].type == "filesystem") {
            fsEntry = &_manifestEntries[i];
            DEBUGOTACLIENT("  Found FS file: %s\n", _manifestEntries[i].name.c_str());
        } else if (_manifestEntries[i].type == "firmware") {
            firmwareEntry = &_manifestEntries[i];
            DEBUGOTACLIENT("  Found firmware file: %s\n", _manifestEntries[i].name.c_str());
        }
    }
    
    // Check versions using core_ota's fileNameCheck + compareWithCurrentFsVersion
    fileCompareResult fwResult, fsResult;
    bool fwValid = false;
    bool fsValid = false;
    
    if (firmwareEntry) {
        int8_t ret = modOtaClass.fileNameCheck(firmwareEntry->name, &fwResult);
        // Only update if server version is NEWER than current
        int8_t fwCompare = modOtaClass.compareWithCurrentFsVersion(&fwResult, firmwareEntry->name);
        fwValid = (ret == 1 && fwCompare == 1);
        DEBUGOTACLIENT("Firmware version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                       fwResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fwResult.nameMatch, fwCompare, fwValid);
    }
    
    if (fsEntry) {
        int8_t ret = modOtaClass.fileNameCheck(fsEntry->name, &fsResult);
        // Only update if server version is NEWER than current
        int8_t fsCompare = modOtaClass.compareWithCurrentFsVersion(&fsResult, fsEntry->name);
        fsValid = (ret == 1 && fsCompare == 1);
        
        // Дополнительная проверка: если версия FS-файла совпадает с кэшированной версией FS — не обновляем
        // (защита от случая, когда compareWithCurrentFsVersion даёт неверный результат)
        if (fsValid) {
            int64_t cachedDate = modOtaClass.getCachedFsDate();
            int32_t cachedBuild = modOtaClass.getCachedFsBuild();
            int32_t cachedMajor = modOtaClass.getCachedFsMajor();
            int32_t cachedMinor = modOtaClass.getCachedFsMinor();
            
            if (cachedDate != 0 || cachedBuild != 0 || cachedMajor != 0 || cachedMinor != 0) {
                // Восстанавливаем версию файла из fsResult.*Diff (которые посчитаны относительно FW)
                // fileVersion = diff + VERSION_*, т.к. fsResult.dateDiff = fileDate - VERSION_DATE
                int64_t fileDate = fsResult.dateDiff + VERSION_DATE;
                int32_t fileBuild = fsResult.isDebug ? (fsResult.buildDiff + VERSION_BUILD) : 0;
                int32_t fileMajor = fsResult.majorDiff + VERSION_MAJOR;
                int32_t fileMinor = fsResult.minorDiff + VERSION_MINOR;
                
                if (fileDate == cachedDate && fileBuild == cachedBuild &&
                    fileMajor == cachedMajor && fileMinor == cachedMinor) {
                    fsValid = false;
                    DEBUGOTACLIENT("FS version matches cached version, skipping\n");
                }
            }
        }
        
        DEBUGOTACLIENT("FS version check: %s (nameMatch=%d, fsCompare=%d, valid=%d)\n",
                       fsResult.nameMatch == 1 ? "MATCH" : "NO MATCH", fsResult.nameMatch, fsCompare, fsValid);
    }
    
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
    if (_fsEnded && modOtaClass._fs) {
        DEBUGOTACLIENT("Remounting filesystem after update attempt...\n");
#if defined(ESP32)
        modOtaClass._fs->begin(true);
#elif defined(ESP8266)
        modOtaClass._fs->begin();
#endif
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

