
#include "core_web/common_module.h"

#include <ESPAsyncWebServer.h>

// ============================================================
// Вспомогательные функции ядра core_web
// ============================================================

namespace ns_core_web {

String getContentType(String filename, AsyncWebServerRequest *request) {
	if 	(request->hasArg("download")) 		{return "application/octet-stream";}
	else if (filename.endsWith(".htm"))  	{return "text/html";}
	else if (filename.endsWith(".html")) 	{return "text/html";}
	else if (filename.endsWith(".css")) 	{return "text/css";}
	else if (filename.endsWith(".json")) 	{return "application/json";}
	else if (filename.endsWith(".js"))   	{return "application/javascript";}
	else if (filename.endsWith(".png"))  	{return "image/png";}
	else if (filename.endsWith(".gif"))  	{return "image/gif";}
	else if (filename.endsWith(".jpg"))  	{return "image/jpeg";}
	else if (filename.endsWith(".ico"))  	{return "image/x-icon";}
	else if (filename.endsWith(".xml"))  	{return "text/xml";}
	else if (filename.endsWith(".pdf"))  	{return "application/x-pdf";}
	else if (filename.endsWith(".zip"))  	{return "application/x-zip";}
	else if (filename.endsWith(".gz"))  	{return "application/x-gzip";}
	else if (filename.endsWith(".hex")) 	{return "text/html";} // TODO ??
	return "text/plain";
}

} // namespace ns_core_web
