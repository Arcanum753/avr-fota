
#include <stdint.h>
#include <stdio.h>
// #include "debug.h"

#if defined(ARDUINO) && ARDUINO >= 100
    #include <Arduino.h>
#else
    #include "WProgram.h"
#endif

#include "common.h"

// TODO Insert to Logseq "Common.h" page
/*
 * hex2bin
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 * returns 0xFF if bad hex digit.
 */
uint8_t hex2bin (uint8_t h)    {
    if (h >= '0' && h <= '9')
       { return(h - '0'); }
    if (h >= 'A' && h <= 'F')
       { return((h - 'A') + 10); }
	if (h >= 'a' && h <= 'f')
       { return((h - 'a') + 10); }
   
    return 0xff;
}

String formatBytes(size_t bytes) {
	if (bytes < 1024) 					{	return String(bytes) + "B";	}
	else
	if (bytes < (1024 * 1024))			{	return String(bytes / 1024.0) + "KB";	}
	else
	if (bytes < (1024 * 1024 * 1024))	{	return String(bytes / 1024.0 / 1024.0) + "MB";	}
	else	{	return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";	}
}


// convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)
unsigned char h2int(char c) {
	if (c >= '0' && c <= '9')	{		return ((unsigned char)c - '0');	}
	if (c >= 'a' && c <= 'f')	{		return ((unsigned char)c - 'a' + 10);	}
	if (c >= 'A' && c <= 'F')	{		return ((unsigned char)c - 'A' + 10);	}
	return(0);
}

String urldecode(String input) { // (based on https://code.google.com/p/avr-netino/)
	char c;
	String ret = "";
	for (byte t = 0; t < input.length(); t++) {
		c = input[t];
		if (c == '+') { c = ' ';}
		if (c == '%') {
			t++;
			c = input[t];
			t++;
			c = (h2int(c) << 4) | h2int(input[t]);
		}
		ret.concat(c);
	}
	return ret;
}

//
// Check the Values is between 0-255
//
boolean checkRange(String Value) {
	if (Value.toInt() < 0 || Value.toInt() > 255) {		return false;	}
	else {		return true;	}
}

// Экранирование для вставки в HTML (div) и защиты разделителей CVT (| и перевод строки)
String escapeHtml(const String& s) {
	String out;
	out.reserve(s.length());
	for (unsigned int i = 0; i < s.length(); i++) {
		char c = s.charAt(i);
		switch (c) {
			case '&':  out += "&amp;";   break;
			case '<':  out += "&lt;";    break;
			case '>':  out += "&gt;";    break;
			case '"':  out += "&quot;";  break;
			case '|':  out += "&#124;";  break;
			case '\r':
			case '\n': out += ' ';       break;
			default:   out += c;         break;
		}
	}
	return out;
}

// Экранирование для вставки в JSON (кавычки, обратный слэш, управляющие символы)
String escapeJson(const String& s) {
	String out;
	out.reserve(s.length());
	for (unsigned int i = 0; i < s.length(); i++) {
		char c = s.charAt(i);
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if ((unsigned char)c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
					out += buf;
				} else {
					out += c;
				}
		}
	}
	return out;
}

