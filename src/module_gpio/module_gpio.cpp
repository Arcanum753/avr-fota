
#include "FSWebServerLib.h"
#include "module_gpio.h"
#include "common.h"
#include "module_gpio_version.h"

MODULE_CLASS_GPIO ModClassGpio(false);

MODULE_CLASS_GPIO :: MODULE_CLASS_GPIO (bool _in) {
	dumb = _in;
}

#if defined(ESP32)
void MODULE_CLASS_GPIO::setFs(fs::SPIFFSFS* fs) //esp32 flash file system
#elif defined(ESP8266)
void MODULE_CLASS_GPIO::setFs(FS* fs)	// esp8266 flash file system
#endif
{	_fs = fs;	}


void  MODULE_CLASS_GPIO::begin(){
	DEBUGGPIO(__FUNCTION__);	DEBUGGPIO("\r\n");
}


void  MODULE_CLASS_GPIO::webInit(){
    DEBUGGPIO(__FUNCTION__);	DEBUGGPIO("\r\n");


//gpio.html vvv
	ESPHTTPServer.on("/gpio", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {    return request->requestAuthentication(); }
		this->gpioGetArgs(request);
	});
//gpio.html ^^^

}


// gpio.html vvv

void  MODULE_CLASS_GPIO::gpioGetArgs(AsyncWebServerRequest *request) {
	DEBUGGPIO(__PRETTY_FUNCTION__);	DEBUGGPIO("\r\n");
	String values = "";
	String uartStr = "";
	if (request->args() > 0) { // get new configs from args
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGGPIO("Arg %d: %s %s\r\n", i, request->argName(i).c_str() , request->arg(i).c_str() );
			if (request->argName(i) == "uartstr")	{
				uartStr = urldecode(request->arg(i));
				Serial.printf("%s \n\r", uartStr.c_str() );
				continue;
			}
			
				if (request->argName(i) == "led1")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MISO, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MISO, LOW);	}
					continue;
				}
				if (request->argName(i) == "led2")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MOSI, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MOSI, LOW);	}
					continue;
				}
				if (request->argName(i) == "led3")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_SCK, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_SCK, LOW);		}
					continue;
				}
				if (request->argName(i) == "led4")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_RST, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_RST, LOW);	}
					continue;
				}
				
		}	
		request->send(200, "text/plain", values);
	}	
}	

// gpio.html ^^^


String MODULE_CLASS_GPIO::getVersionStr(){
    return String(MODULE_GPIO_VERSION);
}

String MODULE_CLASS_GPIO::getGeneratedTime(){
    return String(MODULE_GPIO_GENERATED_TIME);
}

String MODULE_CLASS_GPIO::getCommitDateStr(){
    return String(MODULE_GPIO_COMMIT_DATE_STR);
}


void MODULE_CLASS_GPIO::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGGPIO("%s\n\r", __FUNCTION__);
    String values = "";
    values += "gpioversion|"     + getVersionStr()    + "|dev\n";
    values += "gpiogentime|"     + getGeneratedTime() + "|dev\n";
    values += "gpiogendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}