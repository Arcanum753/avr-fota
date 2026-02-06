
#include <stdint.h>
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


