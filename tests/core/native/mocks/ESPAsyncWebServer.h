#ifndef _MOCK_ESPASYNCWEBSERVER_H
#define _MOCK_ESPASYNCWEBSERVER_H

// Хост-заглушка AsyncWebServer API (только то, что использует ядро).

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <iterator>
#include <map>
#include "Arduino.h"

typedef enum {
    HTTP_ANY = 0,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
    HTTP_OPTIONS
} HTTPMethod;

typedef enum { HTTP_ANY_M = HTTP_ANY } WebRequestMethod;

class AsyncWebServerResponse {
public:
    virtual ~AsyncWebServerResponse() {}
};

class AsyncResponseStream : public AsyncWebServerResponse {
public:
    void print(const char* s) { _buf += (s ? s : ""); }
    void print(const String& s) { _buf += s; }
    void print(char c) { _buf += c; }
    void print(int v) { _buf += String(v); }
    void print(unsigned int v) { _buf += String((unsigned)v); }
    void print(long v) { _buf += String(v); }
    void print(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); _buf += b; }
    void printf(const char* fmt, ...) { (void)fmt; }
    const String& buffer() const { return _buf; }

private:
    String _buf;
};

class AsyncWebServerRequest {
public:
    typedef std::function<void(void)> DisconnectHandler;

    bool hasArg(const String& name) const { return _args.count(name.c_str()) > 0; }
    String arg(const String& name) const {
        auto it = _args.find(name.c_str());
        return it == _args.end() ? String() : String(it->second.c_str());
    }
    size_t args() const { return _args.size(); }
    String argName(size_t i) const {
        auto it = _args.begin();
        std::advance(it, static_cast<long>(i));
        return (it == _args.end()) ? String() : String(it->first.c_str());
    }
    String arg(size_t i) const {
        auto it = _args.begin();
        std::advance(it, static_cast<long>(i));
        return (it == _args.end()) ? String() : String(it->second.c_str());
    }
    void addArg(const String& name, const String& value) { _args[name.c_str()] = value.c_str(); }

    void send(int code) { _lastCode = code; }
    void send(int code, const char* type, const String& content) { (void)type; _lastCode = code; _lastBody = content; }
    void send(int code, const char* type, const char* content) { (void)type; _lastCode = code; _lastBody = content ? content : ""; }
    void send(int code, const char* type, uint8_t* data, size_t len) { (void)type; (void)data; (void)len; _lastCode = code; }
    void send(AsyncWebServerResponse* r) {
        AsyncResponseStream* s = dynamic_cast<AsyncResponseStream*>(r);
        if (s != nullptr) { _streamBody = s->buffer(); }
        _lastCode = 200;
        delete r;
    }
    void send_P(int code, const char* type, const char* content) { send(code, type, content); }
    AsyncResponseStream* beginResponseStream(const String& type) { (void)type; return new AsyncResponseStream(); }
    AsyncResponseStream* beginResponseStream(const char* type) { (void)type; return new AsyncResponseStream(); }
    void requestAuthentication() {}
    void redirect(const String&) {}
    const String& url() const { return _url; }
    void setUrl(const String& u) { _url = u; }
    int lastCode() const { return _lastCode; }
    const String& lastBody() const { return _lastBody; }
    const String& streamBody() const { return _streamBody; }

    void onDisconnect(DisconnectHandler) {}

private:
    std::map<std::string, std::string> _args;
    int    _lastCode = 0;
    String _lastBody;
    String _streamBody;
    String _url;
};

class AsyncWebServerResponse;

class AsyncWebServer {
public:
    explicit AsyncWebServer(uint16_t port = 80) : _port(port) {}
    ~AsyncWebServer() {}

    void begin() {}
    void end() {}
    void addHandler(void*) {}

    template <typename F> void on(const char* url, HTTPMethod method, F handler) { (void)url; (void)method; (void)handler; }
    template <typename F> void on(const char* url, F handler) { (void)url; (void)handler; }
    template <typename F> void on(const String& url, F handler) { (void)url; (void)handler; }
    void onNotFound(std::function<void(AsyncWebServerRequest*)>) {}
    void serveStatic(const char*, const char*, const char* = nullptr) {}

    uint16_t port() const { return _port; }

private:
    uint16_t _port;
};

class AsyncEventSource {
public:
    explicit AsyncEventSource(const char* url) { (void)url; }
    void send(const char*, const char* = nullptr, uint32_t = 0) {}
    void close() {}
    template <typename F> void onConnect(F) {}
};

#endif // _MOCK_ESPASYNCWEBSERVER_H
