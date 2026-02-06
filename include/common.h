
#ifndef _COMMON_h
#define _COMMON_h


uint8_t hex2bin (uint8_t h) ;
String formatBytes(size_t bytes);
boolean checkRange(String Value);
String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
unsigned char h2int(char c);


#endif // _COMMON_h
//eof
