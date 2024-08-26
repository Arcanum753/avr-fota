#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <StreamString.h>
#include "FSWebServerLib.h"
#include "avrisp.h"


SPISettings fuses_spisettings = SPISettings(AVRISP_SPI_FREQLOW,  MSBFIRST, SPI_MODE0);
SPISettings flash_spisettings = SPISettings(AVRISP_SPI_FREQHIGH, MSBFIRST, SPI_MODE0);

extern "C" {
    #include "user_interface.h"
    #include "mem.h"
}

ESP8266_AVRISP avrprog( PIN_RST);

ESP8266_AVRISP::ESP8266_AVRISP( uint8_t reset_pin
                                
                                , bool reset_state
                                , bool reset_activehigh): 
                                  
                                  _reset_pin(reset_pin)
                                , _reset_state(reset_state)
                                , _reset_activehigh(reset_activehigh)   {
    pinMode(_reset_pin, OUTPUT);
    setReset(_reset_state);
}

//all about spi and reset
void ESP8266_AVRISP::setReset(bool rst) {
    _reset_state = rst;
    digitalWrite(_reset_pin, _resetLevel(_reset_state));
}
void ESP8266_AVRISP::setFs (FS* fs){
    _fs = fs;
}
bool ESP8266_AVRISP::begin (){
    cfg_setDefault();  	
    if (!cfgFileLoad()) { 
        cfg_setDefault();  	
        cfgFileSave();
    }

    DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
    return true;
}



avrsip_err_t  ESP8266_AVRISP:: avrChipProgrammDBG(String _in){
    avrsip_err_t _res = ERR_OPENFILE;
    if (_in.isEmpty()){  return _res; }
    if(!cfgFileLoad()) {    return ERR_CFG;   }
    if (_in == "1" ){ _in = _AVRISP_CfgFile.hex_filename;  }
    if (_in == "2"){ _in = _AVRISP_CfgFile.hex_filename_old;  }
    hexFileOpen(_in); 
    hexFileBinDataCheck();
    // dbgPrintVector();
    chipSignRead();
    chipErase();
    hexFile2flashByPages();
    chipFlashVerification();

    AVRISP_HexFileUploaded.buildtime = "Unknown";
    AVRISP_HexFileUploaded.version = "Unknown";

    cfgFilSetUploadeAsNow ( NTP.getTimeDateString());

    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return ERROR_OK;
}

// main procedure of flashing
avrsip_err_t  ESP8266_AVRISP:: avrChipProgrammMain(String _in, String _fwTime){
    avrsip_err_t _res = ERR_OPENFILE;
    if (_in.isEmpty()){   return _res; }
    if(!cfgFileLoad()) {     return ERR_CFG;   }
    if (_in == "1"){ _in = _AVRISP_CfgFile.hex_filename;  }
    if (_in == "2"){ _in = _AVRISP_CfgFile.hex_filename_old;  }

    //open parse & check hexfile
    String _sign = "";
    //read signature
    _sign = chipSignRead();
    if (_sign != _AVRISP_CfgFile.avr_signature) {
        _res = ERR_SIGN;
        return _res;
    }
    //open file
    _res = hexFileOpen(_in); 
    if (_res != ERROR_OK){ return _res;   }

    //check file before flashing
    int32_t _res32  = hexFileBinDataCheck();
    if (_res32 < ERROR_OK){ 
        return _res = (avrsip_err_t) _res32;   }
    //erase chip
    _res = chipErase();
    if (_res != ERROR_OK){ return _res;   }
    //flash chip
    _res = hexFile2flashByPages();
     if (_res != ERROR_OK){ return _res;   }
    
    _res = cfgFileSetNowAsOld ();
    // if (_res != ERROR_OK){ return _res;   }

    _res = cfgFilSetUploadeAsNow (_fwTime);
    // if (_res != ERROR_OK){ return _res;   }

    chipFlashVerification();
	
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return _res;
}

uint16_t ESP8266_AVRISP::chipSpiTransaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint8_t n, m, r;
    SPI.transfer(a); 
    n = SPI.transfer(b);
    //if (n != a) error = -1;
    m = SPI.transfer(c);
    r = SPI.transfer(d);
    return 0xFFFFFF & (((uint32_t)n<<16)+(m<<8) + r);
}

void ESP8266_AVRISP::pmode_begin() {
    pinMode(_reset_pin,  OUTPUT);
    SPI.begin();
    digitalWrite(_reset_pin, _resetLevel(false));
    delayMicroseconds(50);
    digitalWrite(_reset_pin, _resetLevel(true));
    delay(30);
    chipSpiTransaction(STK_PROGMODE);
    DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
}
void ESP8266_AVRISP::pmode_end() {
    SPI.end();
    pinMode(PIN_MISO, INPUT);
    pinMode(PIN_MOSI, INPUT);
    pinMode(PIN_SCK,  INPUT);
    pinMode(PIN_RST,  INPUT);

    digitalWrite(PIN_MISO, LOW);		/* Make sure pullups are off too */
    digitalWrite(PIN_MOSI, LOW);
    digitalWrite(PIN_SCK,  LOW);
    digitalWrite(_reset_pin, _resetLevel(false));
    DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
}

void ESP8266_AVRISP::chipFusesWrite( uint8_t _high, uint8_t _low, uint8_t _lock, uint8_t _ext) {
    AVRISP_fuses_t AVRISP_fuses;
    chipFusesRead(AVRISP_fuses);
   
    if (!_lock) _lock = AVRISP_fuses.lock;
    if (!_low)  _low = AVRISP_fuses.low;
    if (!_high) _high = AVRISP_fuses.high;
    if (!_ext)  _ext = AVRISP_fuses.ext;

    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    if (_lock) {chipSpiTransaction(STK_CHIPFUSELOCK_WR, _lock);     chipBusyWaitPolling();}
    if (_low)  {chipSpiTransaction(STK_CHIPFUSELOW_WR, _low);       chipBusyWaitPolling();}
    if (_high) {chipSpiTransaction(STK_CHIPFUSEHIGH_WR, _high);     chipBusyWaitPolling();}
    if (_ext)  {chipSpiTransaction(STK_CHIPFUSEEXT_WR, _ext);       chipBusyWaitPolling();}
    SPI.endTransaction();
    pmode_end();
    
    DEBUGLOGISP("avr high 0x%02x low  0x%02x  lock  0x%02x ext  0x%02x\r\n",
    _high, _low, _lock, _ext);
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n"); 
}


void ESP8266_AVRISP::chipFusesRead( AVRISP_fuses_t &_AVRISP_fuses ) {
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    _AVRISP_fuses.lock  = chipSpiTransaction(STK_CHIPFUSELOCK);
    _AVRISP_fuses.low   = chipSpiTransaction(STK_CHIPFUSELOW);
    _AVRISP_fuses.high  = chipSpiTransaction(STK_CHIPFUSEHIGH);
    _AVRISP_fuses.ext   = chipSpiTransaction(STK_CHIPFUSEEXT);
    SPI.endTransaction();
    pmode_end();
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n"); 
}

String ESP8266_AVRISP::chipSignRead() {
    char _ret [10] = {0};
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    uint8_t high    = chipSpiTransaction(STK_READSIGN1);
    uint8_t middle  = chipSpiTransaction(STK_READSIGN2);
    uint8_t low     = chipSpiTransaction(STK_READSIGN3);
    SPI.endTransaction();
    pmode_end();
    sprintf (_ret, "0x%02X%02X%02X", high, middle, low);
    DEBUGLOGISP("\t%s \n\r", _ret);
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return String(_ret);
    
}

avrsip_err_t ESP8266_AVRISP::chipErase() {
    avrsip_err_t avrsip_err = ERROR_OK;
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    chipSpiTransaction(STK_ERASECHIP);	// chip erase    
    SPI.endTransaction();
    chipBusyWaitPolling();
    pmode_end();
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return avrsip_err;
}

// Simply polls the chip until it is not busy any more - for erasing and programming
void ESP8266_AVRISP::chipBusyWaitPolling()  {
    uint8_t busybit;
    SPI.beginTransaction(fuses_spisettings);
    do {
        busybit = chipSpiTransaction(STK_POLLCHIP);
    } while ((busybit & 0x01) );
    SPI.endTransaction();
}

avrsip_err_t ESP8266_AVRISP::chipFlashPage (byte *pagebuff, uint16_t pageaddr, uint8_t pagesize) { 
    DEBUGLOGISPBUF("%04x \n\r ", pageaddr);
    SPI.beginTransaction(flash_spisettings);
    for (uint16_t i=0; i < pagesize/2; i++) {
        DEBUGLOGISPBUF("%02x", pagebuff[2*i]);
        DEBUGLOGISPBUF("%02x", pagebuff[2*i+1]);
        if ( i % 8 == 7) DEBUGLOGISPBUF("\n\r");
        if ( i % 4 == 3) DEBUGLOGISP(" ");
        chipFlashWord(LOW, i, pagebuff[2*i]);
        chipFlashWord(HIGH, i, pagebuff[2*i+1]);
    }
    //page addr is in bytes, byt we need to convert to words (/2)
    pageaddr = (pageaddr/2) & (~(((pagesize/2)-1)));
   
    uint16_t commitreply = chipSpiTransaction(0x4C, (pageaddr >> 8) & 0xFF, pageaddr & 0xFF, 0);
    SPI.endTransaction();
    chipBusyWaitPolling();

    DEBUGLOGISPBUF("\tCommit Page: 0x%04x -> 0x%04x\n\r", pageaddr, commitreply);
    if (commitreply != pageaddr) { return ERR_FLASH;  }
    return ERROR_OK; 
}

// Send one byte to the page buffer on the chip
void ESP8266_AVRISP::chipFlashWord (uint8_t hilo, uint16_t addr, uint8_t data) {
    chipSpiTransaction(0x40+8*hilo, addr>>8 & 0xFF, addr & 0xFF, data);
}


// read *.hex file to buf
avrsip_err_t ESP8266_AVRISP::hexFileOpen(String _in){
    if (_in.isEmpty()) {return ERR_NOFILE;}
    String _ret = "";
    if (!_fs){ // If SPIFFS is not started
        _fs->begin();
    }
    if (!_in.startsWith("/")) _in = "/" + _in;
    File hexFile;
    hexFile = _fs->open(_in, "r");
    DEBUGLOGISP(_ret.c_str());
	if (!hexFile) {
        _ret = "Failed to open " + _in +  " file \n\r" ;
		DEBUGLOGISP(_ret.c_str());
		return ERR_OPENFILE;
	}
    _ret = "file " + _in + " open succses!\n\r";
    size_t size = hexFile.size(); //new buf - new size
    _hexFileBuf.clear(); // if was smth? delete.
    _hexFileBuf.resize(size);
    hexFile.readBytes(_hexFileBuf.data(), _hexFileBuf.size());  //open and read file to vector buf
    hexFile.close();    //close file to free fs

    return ERROR_OK;
}



avrsip_err_t  ESP8266_AVRISP:: hexFileMetaStructGet(String _in, AVRISP_HexFileUploaded_t &_inStruct)  {
    avrsip_err_t _ret = ERR_INCORRECTFILE;
    // avrsip_err_t _ret = ERROR_OK;
    AVRISP_HexFileUploaded.hex_filename = _in;
    _ret = hexFileOpen( AVRISP_HexFileUploaded.hex_filename); 
    if (_ret != ERROR_OK){ return _ret;   }
    _ret = hexFileHeadCheck();
    if (_ret != ERROR_OK){ return _ret;   }
    if(!cfgFileLoad()) { 
        return ERR_CFG;
    }
    if (AVRISP_HexFileUploaded.project_name == _AVRISP_CfgFile.project_name ) { 
        AVRISP_HexFileUploaded.cmpproj = true; 
    } else {
        AVRISP_HexFileUploaded.cmpproj = false;
    }

    if (AVRISP_HexFileUploaded.signture == _AVRISP_CfgFile.avr_signature ) { 
        AVRISP_HexFileUploaded.cmpsign = true; 
    } else {
        AVRISP_HexFileUploaded.cmpsign = false;
    }

    _inStruct   = AVRISP_HexFileUploaded;

    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    return _ret ;
}

avrsip_err_t ESP8266_AVRISP::hexFileHeadCheck() {
    avrsip_err_t _ret = ERROR_OK;
    uint32_t _filesize = _hexFileBuf.size();  
    
    AVRISP_HexFileUploaded.signture         = "";
    AVRISP_HexFileUploaded.project_name     = "";
    AVRISP_HexFileUploaded.version          = "";
    AVRISP_HexFileUploaded.buildtime        = "";
    AVRISP_HexFileUploaded.size             = 0;
    char _c;
    bool _isParam = false;

    bool _isValueSign = false; 
    bool _isValueName = false; 
    bool _isValueVers = false; 
    // bool _isValueDate = false; 
    bool _isValueTime = false; 
    String _strParam = "";

    for (uint32_t _index = 0; _index < _filesize; _index++) {
        _c = _hexFileBuf.at(_index);

        if   (_c =='\n' || _c  ==  '\r' || _c  == '\t') { 
            if (_isValueSign)  _isValueSign = false;  
            if (_isValueName)  _isValueName = false; 
            if (_isValueVers)  _isValueVers = false; 
            // if (_isValueDate)  _isValueDate = false; 
            if (_isValueTime)  _isValueTime = false; 
        }

        // if ( _c != HEX_PARSE_CHAR_SPACE )    {
            if(_isValueSign ) AVRISP_HexFileUploaded.signture       += _c;  
            if(_isValueName ) AVRISP_HexFileUploaded.project_name   += _c;  
            if(_isValueVers ) AVRISP_HexFileUploaded.version        += _c;  
            // if(_isValueDate ) AVRISP_HexFileUploaded.buildDate      += _c;  
            if(_isValueTime ) AVRISP_HexFileUploaded.buildtime      += _c;  
        // }


        if (_strParam == HEX_PARSE_WORD_SIGN)           { _isValueSign = true;  }
        if (_strParam == HEX_PARSE_WORD_PROJ)           { _isValueName = true;  }
        if (_strParam == HEX_PARSE_WORD_VER)            { _isValueVers = true;  }
        // if (_strParam == HEX_PARSE_WORD_DATE)           { _isValueDate = true;  }
        if (_strParam == HEX_PARSE_WORD_TIME)           { _isValueTime = true;  }

        if (_isParam && _c != HEX_PARSE_METALINEBEGIN)      { _strParam += _c; }
        if (_isParam && _c == HEX_PARSE_CHAR_SPACE)         { _isParam = false;  _strParam = ""; }
        if (_c == HEX_PARSE_METALINEBEGIN)                  { _isParam = true;   }

    } 
#if (DEBUG_SHOWHEXBUF > 2)
    DEBUGLOGISP("sign >%s<\n\r",  AVRISP_HexFileUploaded.signture.c_str());
    DEBUGLOGISP("proj >%s<\n\r",  AVRISP_HexFileUploaded.project_name.c_str());
    DEBUGLOGISP("vers >%s<\n\r",  AVRISP_HexFileUploaded.version.c_str());
    DEBUGLOGISP("time >%s<\n\r",  AVRISP_HexFileUploaded.buildtime.c_str());
#endif
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return _ret;
}
int32_t  ESP8266_AVRISP:: hexFileUploadedBodyCheck(String _in){
    hexFileOpen(_in); 
    int32_t _ret  = hexFileBinDataCheck();
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
   
    return _ret ;
}


// Returns 0 if hex file is ok.
int32_t ESP8266_AVRISP::hexFileBinDataCheck (  )   {
    int32_t _ret = ERROR_OK;
    if(_hexFileBuf.empty()){
        return ERR_NOFILE; 
    }
    // filesize fo all file bufer
    uint32_t filesize = _hexFileBuf.size();
    //set size of bindata only

    
    uint32_t hexTrueLineBegin = 0;     // pos of symbol after first ':'
    uint32_t numOfDotLines = 0;        // how many lines we have
    char _c;  
    // look for first ':' and remember position of next symb. now it is our new begin of file.
    for (uint32_t _index = 0; _index < filesize; _index++) {
        _c = _hexFileBuf.at(_index);
        if (_c == HEX_PARSE_LINEBEGIN) {
            numOfDotLines ++;
            if(!hexTrueLineBegin) {            //worked once
                hexTrueLineBegin = _index + 1; // ':'+1 symbol
            }
        }
    }
    DEBUGLOGISP("numOfDotLines %d\n\r", numOfDotLines);
    if (!hexTrueLineBegin ) return ERR_INCORRECTFILE;  //if no ':' - return error

    _hexFileBinDataBuf.clear();
    _hexFileBinDataBuf.resize(filesize);
    uint32_t binBufIndex = 0;

    uint16_t pageaddr = 0;                          // read addr from hex file line 
    uint16_t pageaddrPrev = 0;                          // read addr from hex file prev line 
    uint32_t  pagesize = _AVRISP_CfgFile.pagesize;  // size of page buf.
    uint32_t  chipmemsize = _AVRISP_CfgFile.chipsize;  // size of mem chip.
    uint8_t  lineBuffer[pagesize];                  // buf for parsed line data. size same as page
    int32_t  readedSyms = 0;                        // how many symb's readed in this line
    uint8_t  readedBins = 0;                        // how many BIN data readed in this line
    uint8_t  chsum = 0;                             // check sum
    uint8_t  rtype = 0;                             // record type
    uint32_t  totalBins = 0;                        // how many symb's readed in this line
    for (uint32_t _linepos = 0; _linepos < numOfDotLines; _linepos++) {
        readedSyms = hexFileLineParser(hexTrueLineBegin, pageaddr, lineBuffer, chsum,  rtype, readedBins, totalBins);
        DEBUGLOGISPBUF("pageaddr 0x%04x _linepos %d , readedSyms %d , chsum 0x%02x,  rtype 0x%02x, readedBins %d , totalBins %u\n\r ", 
                    pageaddr, _linepos, readedSyms, chsum,  rtype, readedBins, totalBins);
        
        hexTrueLineBegin += readedSyms;
        if (pageaddrPrev > pageaddr && !rtype){
            DEBUGLOGISP("incorrect addr error!\n\r");
            _ret = ERR_HEXADDR;
            break;
        } else {
            pageaddrPrev = pageaddr;
            
        }
        if (chsum && !rtype) {
             _ret = ERR_HEXCRC;
             break;
        }
        if (readedSyms == 0){
            DEBUGLOGISP("hexFileLineParser %d   error\n\r", _linepos);
            _ret = ERR_INCORRECTFILE;
            break;
        } 
        if (totalBins >= chipmemsize){
            _ret = ERR_HEXMEMOVER;
            break;
        } else {
            _ret = totalBins;
        }
        if (readedSyms == - 1){
            DEBUGLOGISPBUF("eof!\n\r");
            break;
        }
        if (readedSyms > 0){
            for (uint16_t i = 0; i < readedBins; i++) {
                _hexFileBinDataBuf.at(binBufIndex++ ) = lineBuffer[i];
            #if (DEBUG_SHOWHEXBUF > 2)
                DEBUGLOGISPBUF("%02x", lineBuffer[i]);
                if ( i % 8 == 7) DEBUGLOGISPBUF(" ");
                // if ( i % 16 == 15) DEBUGLOGISP("\n\r");
            #endif
            }
            DEBUGLOGISPBUF("\n\r");
        }
    }
    if (binBufIndex == totalBins) {DEBUGLOGISP ("binBufIndex == totalBins\n\r");}
    AVRISP_HexFileUploaded.size = totalBins;
    
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return _ret;
}

void  ESP8266_AVRISP::dbgPrintVector(){
    DEBUGLOGISP("dbgPrintVector vvv\r\n");
    for (uint32_t i = 0; i < AVRISP_HexFileUploaded.size; i++) {
        DEBUGLOGISP("%02x", _hexFileBinDataBuf.at(i));
        if ( i % 8 == 7) DEBUGLOGISP(" ");
        if ( i % 16 == 15) DEBUGLOGISP("\n\r");
    }
    DEBUGLOGISP("\r\ndbgPrintVector ^^^\r\n");
}

avrsip_err_t  ESP8266_AVRISP::hexFile2flashByPages( ){
   avrsip_err_t _ret = ERROR_OK;
    if(_hexFileBuf.empty()){
        return ERR_NOFILE; 
    }
    uint32_t filesize = _hexFileBuf.size();
    uint32_t hexTrueLineBegin = 0;     // pos of symbol after first ':'
    uint32_t numOfDotLines = 0;        // how many lines we have
    char _c;  
    // look for first ':' and remember position of next symb. now it is our new begin of file.
    for (uint32_t _index = 0; _index < filesize; _index++) {
        _c = _hexFileBuf.at(_index);
        if (_c == HEX_PARSE_LINEBEGIN) {
            numOfDotLines ++;
            if(!hexTrueLineBegin) {            //worked once
                hexTrueLineBegin = _index + 1; // ':'+1 symbol
            }
        }
    }
    if (!hexTrueLineBegin ) return ERR_INCORRECTFILE;  //if no ':' - return error

    uint16_t  linepageaddr = 0;                          // read addr from hex line file
    uint32_t  pagesize = _AVRISP_CfgFile.pagesize;  // size of page buf.
    uint8_t   lineBuffer[pagesize];                  // buf for parsed line data. size same as page
    int32_t   readedSyms = 0;                        // how many symb's readed in this line
    uint8_t   readedBins = 0;                        // how many BIN data readed in this line
    uint8_t   chsum = 0;                             // check sum
    uint8_t   rtype = 0;                             // record type
    uint32_t  totalBins = 0;                        // how many symb's readed in this line

    uint8_t  spipageBuffer[pagesize];                  // buf for page
    uint16_t spiBufPos = 0;                    // bin page counter
    uint16_t spipageaddr = 0;                       //
    pmode_begin();
    for (uint16_t y = 0; y < pagesize; y++) spipageBuffer[y] = 0xff; // fill page buf with 0xff (0 in flash mem)
    for (uint32_t _linepos = 0; _linepos < numOfDotLines; _linepos++) {
        readedSyms = hexFileLineParser(hexTrueLineBegin, linepageaddr, lineBuffer, chsum,  rtype, readedBins, totalBins);
        hexTrueLineBegin += readedSyms;
        if (spiBufPos && rtype){
            _ret = chipFlashPage (spipageBuffer, spipageaddr, pagesize); 
        }
        if (spiBufPos >= pagesize ) {
            spiBufPos = 0;
            _ret = chipFlashPage (spipageBuffer, spipageaddr, pagesize);
            for (uint16_t y = 0; y < pagesize; y++) spipageBuffer[y] = 0xff; // fill page buf with 0xff (0 in flash mem)
            spipageaddr += pagesize;
        }
        for (uint16_t i = 0; i < readedBins; i++) {
            spipageBuffer[spiBufPos++] = lineBuffer[i];
        }
        if (_ret != ERROR_OK) {
            break;
        }
        
    }
    pmode_end();
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
    return _ret;
}

// return num of char symbols read. if error then < 0
int32_t ESP8266_AVRISP::hexFileLineParser (uint32_t beginLine, uint16_t &linepageaddr, byte *lineBuf,  uint8_t  &chsum, uint8_t &type, uint8_t &binReadNum, uint32_t &totalBins )   {
    int32_t lineLen = 0; // line lenght
    uint32_t _filesize = _hexFileBuf.size();
    uint16_t len;
    uint8_t b = 0;
    char _c;  

    // 'empty' the page by filling it with 0xFF's
    for (uint32_t _index = beginLine; _index < _filesize; _index++) {
         _c = _hexFileBuf.at(_index);
        lineLen++;
        if (_c == HEX_PARSE_LINEBEGIN) { 
            break;
        }
    }
    // for (uint8_t i=0; i < lineLen; i++) { lineBuf[i] = 0xFF;   }
    // read lenght
    len =            hex2bin(_hexFileBuf.at(beginLine++));
    len = (len<<4) + hex2bin(_hexFileBuf.at(beginLine++));
    chsum = len;
    
    // read high address byte
    b =           hex2bin(_hexFileBuf.at(beginLine++));
    b = (b<<4) +  hex2bin(_hexFileBuf.at(beginLine++));
    chsum += b;
    linepageaddr = b;

    // read low address byte
    b =           hex2bin(_hexFileBuf.at(beginLine++));
    b = (b<<4) +  hex2bin(_hexFileBuf.at(beginLine++));
    chsum += b;
    linepageaddr = (linepageaddr << 8) + b;

    // record type 
    b =            hex2bin(_hexFileBuf.at(beginLine++)); // record type 
    b = (b<<4) +   hex2bin(_hexFileBuf.at(beginLine++));
    chsum += b;
    type = b;
    if (type == 0x01){
        // end record, return -1 to indicate we're done
        return - 1; 
    }
    // read line 
    binReadNum = 0;
    for (byte i=0; i < len; i++) {
        // read 'n' bytes
        b =          hex2bin(_hexFileBuf.at(beginLine++));
        b = (b<<4) + hex2bin(_hexFileBuf.at(beginLine++)); 
        lineBuf[i] = b;
        chsum += b;
        binReadNum ++;
        totalBins++;
    }       
    //check sum, 
    b =           hex2bin(_hexFileBuf.at(beginLine++));  
    b = (b<<4) +  hex2bin(_hexFileBuf.at(beginLine++));
    chsum += b;

    return lineLen;
}


/*
 * hex2bin
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 * returns 0xFF if bad hex digit.
 */
uint8_t ESP8266_AVRISP::hex2bin (uint8_t h)    {
    if (h >= '0' && h <= '9')
        return(h - '0');
    if (h >= 'A' && h <= 'F')
        return((h - 'A') + 10);
    DEBUGLOGISP("Bad hex digit! \n\r");
    return 0xff;
} 

/*------------------------------------------------------------------------*/
//chip flash verification 
// return 0 if all ok
// return addr where is mistake
String ESP8266_AVRISP::chipFlashVerification() {
    String _ret = "";
    char strbuf[256];
    uint32_t addr = 0;
    uint8_t bytebuf = 0;
    uint8_t bytespi = 0;
    pmode_begin();
    for (addr = 0; addr < AVRISP_HexFileUploaded.size; addr++){
        bytebuf = _hexFileBinDataBuf.at(addr);
// read this byte
        SPI.beginTransaction(flash_spisettings);
        if (addr % 2) {
            // for 'high' bytes:
            bytespi = chipSpiTransaction(STK_VERIFYADDRHIGH, addr >> 9, addr / 2, 0) & 0xFF;
        } else {
            // for 'low bytes'
            bytespi = chipSpiTransaction(STK_VERIFYADDRLOW,  addr >> 9, addr / 2, 0) & 0xFF; 
        }
        SPI.endTransaction();
// verify this byte
        if (bytebuf != bytespi){
            sprintf(strbuf, "Verification error at address 0x%04x. <br> Should be 0x%02x not 0x%02x", addr, bytebuf, bytespi);
            _ret = (String)strbuf;
            DEBUGLOGISP("%s \n\r", strbuf);
            
            pmode_end();
            return _ret;
        }
    }
    pmode_end();
    sprintf(strbuf, "Verification succesfull! <br> All %d bytes are correct!<br>", addr);
    AVRISP_fuses_t AVRISP_fuses ;
    chipFusesRead(AVRISP_fuses);
    sprintf(strbuf, "Fuses: HIGH 0x%02x LOW 0x%02x LOCK 0x%02x EXT 0x%02x<br>", 
        AVRISP_fuses.high, AVRISP_fuses.low, AVRISP_fuses.lock, AVRISP_fuses.ext
        );
    verificationResult = _ret;
    DEBUGLOGISP("%s \n\r", strbuf);
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    return _ret;

}

String  ESP8266_AVRISP:: chipFlashVerificationResultGet(){
    return verificationResult;
}



/*------------------------------------------------------------------------*/
//all about json & configs

avrsip_err_t  ESP8266_AVRISP:: cfgFileStructGet(AVRISP_CfgFile_t &_inStruct)  {
    avrsip_err_t _ret = ERROR_OK;
    if(!cfgFileLoad()) {  return ERR_CFG; }
    _inStruct = _AVRISP_CfgFile;
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    return _ret ;
}

void ESP8266_AVRISP::cfgFileLoadWeb(AVRISP_CfgFile_t &_inStruct){
    _AVRISP_CfgFile.avr_signature          =  _inStruct.avr_signature;
    _AVRISP_CfgFile.project_name           =  _inStruct.project_name;
    _AVRISP_CfgFile.chipsize               =  _inStruct.chipsize;
    cfgFileSave();
}

bool ESP8266_AVRISP::cfgFileLoad() {
    if (!_fs) // If SPIFFS is not started
            {_fs->begin();
    }
	File configFile = _fs->open(DEFAULT_PROG_JSON, "r");	
	if (!configFile) {
		DEBUGLOGISP("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	if (size > JSON_FILESIZEMAX) {
        DEBUGLOGISP("Config file size is too large");
        configFile.close();
        return false;
	}
	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DynamicJsonDocument jsonDoc(JSON_FILESIZEMAX);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUGLOGISP("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

    _AVRISP_CfgFile.hex_filename           = jsonDoc["hex_filename"]     .as<const char *>();
    _AVRISP_CfgFile.hex_filename_old       = jsonDoc["hex_filename_old"] .as<const char *>();
    _AVRISP_CfgFile.hex_version            = jsonDoc["hex_vesion"]       .as<const char *>();
    _AVRISP_CfgFile.hex_version_old        = jsonDoc["hex_vesion_old"]   .as<const char *>();
    _AVRISP_CfgFile.hex_buildtime          = jsonDoc["hex_ts"]           .as<const char *>();
    _AVRISP_CfgFile.hex_buildtime_old      = jsonDoc["hex_ts_old"]       .as<const char *>();
    _AVRISP_CfgFile.fwTS                   = jsonDoc["lastUpdTS_gmt"]    .as<const char *>();
    
    _AVRISP_CfgFile.avr_signature          = jsonDoc["avr_sign"].as<const char *>();
    _AVRISP_CfgFile.project_name           = jsonDoc["avr_proj"].as<const char *>();
    _AVRISP_CfgFile.chipsize               = jsonDoc["chipsize"].as<uint32_t>();
    _AVRISP_CfgFile.pagesize               = jsonDoc["pagesize"].as<uint32_t>();
#if (DEBUG_SHOWHEXBUF > 2)
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp);
#endif
	DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
	return true;
}

void ESP8266_AVRISP::cfg_setDefault() {
	_AVRISP_CfgFile.hex_filename            = DEFAULT_HEXFILENAME;
	_AVRISP_CfgFile.hex_filename_old        = DEFAULT_HEXFILENAMEOLD;
	_AVRISP_CfgFile.hex_version             = DEFAULT_VER;
	_AVRISP_CfgFile.hex_version_old         = DEFAULT_VEROLD;
	_AVRISP_CfgFile.hex_buildtime           = "";
	_AVRISP_CfgFile.hex_buildtime_old       = "";
    _AVRISP_CfgFile.fwTS                    = "";

	_AVRISP_CfgFile.avr_signature                 = DEFAULT_AVR_SIGN;
    _AVRISP_CfgFile.project_name                = DEFAULT_AVR_MCU;  
    _AVRISP_CfgFile.chipsize               = DEFAULT_chipsize; 
    _AVRISP_CfgFile.pagesize               = MEM_PAGE_SIZE;     

	AVRISP_HexFileUploaded.hex_filename = ""; 
    AVRISP_HexFileUploaded.signture = ""; 
    AVRISP_HexFileUploaded.project_name = ""; 
    AVRISP_HexFileUploaded.version = ""; 
    AVRISP_HexFileUploaded.buildtime = ""; 

	DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
}

bool ESP8266_AVRISP::cfgFileSave(){
	DynamicJsonDocument jsonDoc(JSON_STR_LEN);
	jsonDoc["hex_filename"]         = _AVRISP_CfgFile.hex_filename;
	jsonDoc["hex_filename_old"]     = _AVRISP_CfgFile.hex_filename_old  ;
	jsonDoc["hex_vesion"]           = _AVRISP_CfgFile.hex_version;
	jsonDoc["hex_vesion_old"]       = _AVRISP_CfgFile.hex_version_old;
	jsonDoc["hex_ts"]               = _AVRISP_CfgFile.hex_buildtime;
	jsonDoc["hex_ts_old"]           = _AVRISP_CfgFile.hex_buildtime_old;
    jsonDoc["lastUpdTS_gmt"]        = _AVRISP_CfgFile.fwTS;

    jsonDoc["avr_sign"]               = _AVRISP_CfgFile.avr_signature;
    jsonDoc["avr_proj"]              = _AVRISP_CfgFile.project_name;
    jsonDoc["chipsize"]             = _AVRISP_CfgFile.chipsize;
    jsonDoc["pagesize"]             = _AVRISP_CfgFile.pagesize;
    if (!_fs) // If SPIFFS is not started
        {_fs->begin();
    }
	File configFile  = _fs->open(DEFAULT_PROG_JSON, "w");	
	if (!configFile) {
		DEBUGLOGISP("\r\n Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
    DEBUGLOGISP("\r\n");
	DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
	return true;
}

avrsip_err_t  ESP8266_AVRISP:: cfgFilSetUploadeAsNow (String _fwTime){
    avrsip_err_t _ret = ERR_RNM;
    if(cfgFileLoad()) {
        _ret = ERROR_OK; 
    } else {
        return ERR_OPENFILE;
    }
    DEBUGLOGISP("  hex_filename %s\n\r ",   AVRISP_HexFileUploaded.hex_filename.c_str());
    DEBUGLOGISP("  hex_version %s\n\r ",    AVRISP_HexFileUploaded.version.c_str());
    DEBUGLOGISP("  hex_buildtime %s\n\r ",  AVRISP_HexFileUploaded.buildtime.c_str());
    DEBUGLOGISP("  _fwTime %s\n\r ",  _fwTime.c_str());

    _AVRISP_CfgFile.hex_filename   =   AVRISP_HexFileUploaded.hex_filename;
    _AVRISP_CfgFile.hex_version    =   AVRISP_HexFileUploaded.version;
    _AVRISP_CfgFile.hex_buildtime  =   AVRISP_HexFileUploaded.buildtime;
    _AVRISP_CfgFile.fwTS           =   _fwTime;


    if(cfgFileSave()) {
       _ret = ERROR_OK; 
    } else {
        _ret = ERR_CFG;
    }
    DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
    return _ret;
}

avrsip_err_t  ESP8266_AVRISP:: cfgFileSetNowAsOld(){
    avrsip_err_t _ret = ERR_RNM;
    if(cfgFileLoad()) {
        _ret = ERROR_OK; 
    } else {
        return ERR_OPENFILE;
    }
    if (_AVRISP_CfgFile.hex_filename_old == _AVRISP_CfgFile.hex_filename) {
        return _ret;
    }
//prepare vars
    String filename_old = _AVRISP_CfgFile.hex_filename_old;
    if (!filename_old.startsWith("/")) filename_old = "/" + filename_old;

// delete old file
    if (_fs->exists(filename_old)){
        if ( _fs->remove(filename_old)) {
            DEBUGLOGISP("old Hex file %s removed OK\r\n", filename_old.c_str());
        } else {
            DEBUGLOGISP("old Hex file %s NOT removed\r\n", filename_old.c_str());
        }
    } else {
        DEBUGLOGISP("old Hex file %s NOT EXIST\r\n", filename_old.c_str());
            _ret = ERR_NOFILE;
    }

// set "now" cfg as "old" cfg
    _AVRISP_CfgFile.hex_filename_old   =   _AVRISP_CfgFile.hex_filename;
    _AVRISP_CfgFile.hex_version_old    =   _AVRISP_CfgFile.hex_version;
    _AVRISP_CfgFile.hex_buildtime_old  =   _AVRISP_CfgFile.hex_buildtime;

    if(cfgFileSave()) {
       _ret = ERROR_OK; 
    } else {
        _ret = ERR_CFG;
    }

	DEBUGLOGISP(" %d \r\n" , _ret);
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
   
    return _ret ;
}


// DEBUG
String ESP8266_AVRISP::fsDirListGet() {
    String list = "";
    if (!_fs) // If SPIFFS is not started
            _fs->begin();
    Dir dir = _fs->openDir("/");
    while (dir.next()) {
        list += dir.fileName() + "; ";
        File f = dir.openFile("r");
        list += String(f.size()) + "; \n\r";
    }
	DEBUGLOGISP("\r\n");
    DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");

    return list;
}



