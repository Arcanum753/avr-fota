
#include "FSWebServerLib.h"
#include "module_gpio.h"
#include "common.h"
#include "module_gpio_version.h"

CLASS_MODULE_GPIO ModClassGpio(false);

CLASS_MODULE_GPIO :: CLASS_MODULE_GPIO (bool _in) {
	dumb = _in;
}

#if defined(ESP32)
void CLASS_MODULE_GPIO::setFs(fs::LittleFSFS* fs) //esp32 flash file system
#elif defined(ESP8266)
void CLASS_MODULE_GPIO::setFs(FS* fs)	// esp8266 flash file system
#endif
{	_fs = fs;	}


void  CLASS_MODULE_GPIO::begin(){
	DEBUGGPIO(__FUNCTION__);	DEBUGGPIO("\r\n");
}

void  CLASS_MODULE_GPIO::begin(ModContext& ctx){
	_fs = ctx.fs;
	begin();
}


void  CLASS_MODULE_GPIO::web_Init(){
    DEBUGGPIO(__FUNCTION__);	DEBUGGPIO("\r\n");


//gpio.html vvv
	ESPHTTPServer.on("/gpio", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {    return request->requestAuthentication(); }
		this->gpioGetArgs(request);
	});
//gpio.html ^^^

}


// gpio.html vvv

void  CLASS_MODULE_GPIO::gpioGetArgs(AsyncWebServerRequest *request) {
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


String CLASS_MODULE_GPIO::getVersionStr(){
    return String(MODULE_GPIO_VERSION);
}

String CLASS_MODULE_GPIO::getGeneratedTime(){
    return String(MODULE_GPIO_GENERATED_TIME);
}

String CLASS_MODULE_GPIO::getCommitDateStr(){
    return String(MODULE_GPIO_COMMIT_DATE_STR);
}


void CLASS_MODULE_GPIO::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGGPIO("%s\n\r", __FUNCTION__);
    String values = "";
    values += "gpioversion|"     + getVersionStr()    + "|div\n";
    values += "gpiogentime|"     + getGeneratedTime() + "|div\n";
    values += "gpiogendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}