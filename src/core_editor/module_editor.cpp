
#include "FSWebServerLib.h"
#include "module_editor.h"
#include "main.h"

MODULE_CLASS_EDITOR ModClassEdit(false);

MODULE_CLASS_EDITOR :: MODULE_CLASS_EDITOR (bool _in) {
	dumb = _in;
}

#if defined(ESP32)
void MODULE_CLASS_EDITOR::setFs(fs::SPIFFSFS* fs) //esp32 flash file system
#elif defined(ESP8266)
void MODULE_CLASS_EDITOR::setFs(FS* fs)	// esp8266 flash file system
#endif
{	_fs = fs;	}


void  MODULE_CLASS_EDITOR::begin(){
	DEBUGEDIT(__FUNCTION__);	DEBUGEDIT("\r\n");
}


void  MODULE_CLASS_EDITOR::webInit(){
    DEBUGEDIT(__FUNCTION__);	DEBUGEDIT("\r\n");
//edit.html vvv

    //list directory
    ESPHTTPServer.on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {    return request->requestAuthentication(); }
        this->handleFileList(request);
    });

    //load editor
    ESPHTTPServer.on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {    return request->requestAuthentication(); }
        if (!ESPHTTPServer.handleFileRead("/edit.html", request))
            {   request->send(404, "text/plain", "FileNotFound");   }
    });

    //create file
    ESPHTTPServer.on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {    return request->requestAuthentication(); }
        this->handleFileCreate(request);
    });	

    //delete file
    ESPHTTPServer.on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
        this->handleFileDelete(request);
    });

    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    ESPHTTPServer.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); },
        [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            this->handleFileUpload(request, filename, index, data, len, final);
    });
//edit.html ^^^

}

void MODULE_CLASS_EDITOR::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	static File fsUploadFile;
	static size_t fileSize = 0;

	if (!index) { // Start
		DEBUGEDIT("handleFileUpload Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) filename = "/" + filename;
		fsUploadFile = _fs->open(filename, "w");
		DEBUGEDIT("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGEDIT("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) != len) {	DEBUGEDIT("Write error during upload. \r\n");	}
		else {	fileSize += len;	}
	}
	//da fack?!
	/*for (size_t i = 0; i < len; i++) {
	if (fsUploadFile)
	fsUploadFile.write(data[i]);
	}*/
	if (final) { // End
		if (fsUploadFile) {	fsUploadFile.close();	}
		DEBUGEDIT("handleFileUpload Size: %u\n", fileSize);
		fileSize = 0;
	}
}


void MODULE_CLASS_EDITOR::handleFileList(AsyncWebServerRequest *request) {
	if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }
	String path = request->arg("dir");
	DEBUGEDIT("handleFileList: %s\r\n", path.c_str());
	String output = "[";

#ifdef ESP32
	File root =  _fs->open(path);
	File file = root.openNextFile();
	while (file) {
		if (output != "[")	{output += ',';}
		bool isDir = false;
		output += "{\"type\":\"";
		isDir = file.isDirectory();
		output += (isDir) ? "dir" : "file";
		output += "\",\"name\":\"";
		output += String(file.name());
		output += "\"}";
		file = root.openNextFile();
	}
	#else
	Dir dir = _fs->openDir(path);
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (true)//entry.name()!="secret.json") // Do not show secrets
		{
			if (output != "[")	{output += ',';}
			bool isDir = false;
			output += "{\"type\":\"";
			output += (isDir) ? "dir" : "file";
			output += "\",\"name\":\"";
			output += String(entry.name()).substring(1);
			output += "\"}";
		}
		entry.close();
		}
#endif

	output += "]";
	DEBUGEDIT("%s\r\n", output.c_str());
	request->send(200, "text/json", output);
}



void MODULE_CLASS_EDITOR::handleFileCreate(AsyncWebServerRequest *request) {
	if (request->args() == 0)		{	return request->send(500, "text/plain", "BAD ARGS");}
	String path = request->arg(0U);
	DEBUGEDIT("handleFileCreate: %s\r\n", path.c_str());
	if (path == "/")			{	return request->send(500, "text/plain", "BAD PATH");	}
	if (_fs->exists(path))		{	return request->send(500, "text/plain", "FILE EXISTS");	}
	File file = _fs->open(path, "w");
	if (file)	{	file.close();	}
	else		{	return request->send(500, "text/plain", "CREATE FAILED");	}
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}


// удаление файла

void MODULE_CLASS_EDITOR::handleFileDelete(AsyncWebServerRequest *request) {
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = request->arg(0U);
	DEBUGEDIT("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/") 		{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!_fs->exists(path)) {	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
	request->send(200, "text/plain", "");
}

