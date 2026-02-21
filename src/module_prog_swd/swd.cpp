
// Many thanks to scanlime for the work on the ESP8266 SWD Library, parts of this code have inspiration and help from it
// https://github.com/scanlime/esp8266-arm-swd

#include "main.h"
#ifdef  PROGTYPE_SWD


#include "Arduino.h"
#include "swd.h"

#include "FSWebServerLib.h"
// #include "debug.h"
#include "debug_cm.h"

bool gpioInitState = false;
bool turn_state = 0;


void swd_gpio_init()  {
  if (!gpioInitState) { gpioInitState = true; }
  pinMode(swd_data_pin,   INPUT_PULLUP);
  pinMode(swd_clock_pin,  OUTPUT);
}

uint32_t swd_init() { //Returns the ID
  DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
  swd_write(0xffffffff, 32);
  swd_write(0xffffffff, 32);
  swd_write(0xe79e, 16);
  swd_write(0xffffffff, 32);
  swd_write(0xffffffff, 32);
  swd_write(0, 32);
  swd_write(0, 32);

  uint32_t idcode;
  swd_DP_Read(DP_IDCODE, idcode);
  DEBUGLOG("idcode: %08x \r\n", idcode);
  return idcode;
}

// write is 0 before data // AP is 1 before data
// Access Port
bool swd_AP_Write(unsigned addr, uint32_t data) {
  uint8_t retry = 15;
  while (retry--)   {
    bool state = swd_transfer(addr, 1, 0, data);
    if (state)  {  return true; }
  }
  return false;
}

// read is 1 before data // AP is 1 before data
// Access Port
bool swd_AP_Read(unsigned addr, uint32_t &data) {
  uint8_t retry = 15;
  while (retry--)  {
    bool state = swd_transfer(addr, 1, 1, data);
    if (state) {  return true;}
  }
  return false;
}
// write is 0 before data // DP is 0 before data
bool swd_DP_Write(unsigned addr, uint32_t data) {
  uint8_t retry = 15;
  while (retry--)  {
    bool state = swd_transfer(addr, 0, 0, data);
    if (state) {  return true; }
  }
  return false;
}
// read is 1 before data // DP is 0 before data
bool swd_DP_Read(unsigned addr, uint32_t &data) {
  uint8_t retry = 15;
  while (retry--)   {
    bool state = swd_transfer(addr, 0, 1, data);
    if (state){ return true;}
  }
  return false;
}

// port , ap = 1, ad = 0, read 1 write 0
bool swd_transfer(unsigned port_address, bool APorDP, bool RorW, uint32_t &data)  {
  bool parity = APorDP ^ RorW ^ ((port_address >> 2) & 1) ^ ((port_address >> 3) & 1);
  uint8_t filled_address = (1 << 0) | (APorDP << 1) | (RorW << 2) | ((port_address & 0xC) << 1) | (parity << 5) | (1 << 7);
  swd_write(filled_address, 8);
  if (swd_read(3) == 1)  {
    if (RorW)    { // Reading 32 bits from SWD
      data = swd_read(32);
      if (swd_read(1) == swd_calculate_parity(data))      {
        swd_write(0, 1);
        return true;
      }
    }
    else  { // Writing 32bits to SWD
      swd_write(data, 32);
      swd_write(swd_calculate_parity(data), 1);
      swd_write(0, 1);
      return true;
    }
  }
  swd_write(0, 32);
  return false;
}




bool swd_calculate_parity(uint32_t in_data) {
  in_data = (in_data & 0xFFFF)  ^ (in_data >> 16);
  in_data = (in_data & 0xFF)    ^ (in_data >> 8);
  in_data = (in_data & 0xF)     ^ (in_data >> 4);
  in_data = (in_data & 0x3)     ^ (in_data >> 2);
  in_data = (in_data & 0x1)     ^ (in_data >> 1);
  return in_data;
}



void swd_write(uint32_t in_data, uint8_t bits) {
    if (turn_state == 0) {	swd_turn(1);	}
    while (bits--)  {
        digitalWrite(swd_data_pin, in_data & 1);
        digitalWrite(swd_clock_pin, LOW);
        delayMicroseconds(SWD_DELAY);
        in_data >>= 1;
        digitalWrite(swd_clock_pin, HIGH);
        delayMicroseconds(SWD_DELAY);
    }
}


uint32_t swd_read(uint8_t bits) {
  uint32_t out_data = 0;
  uint32_t input_bit = 1;
  if (turn_state == 1)  { swd_turn(0);  }
  while (bits--)  {
    if (digitalRead(swd_data_pin))    { out_data |= input_bit;  }
    digitalWrite(swd_clock_pin, LOW);
    delayMicroseconds(SWD_DELAY);
    input_bit <<= 1;
    digitalWrite(swd_clock_pin, HIGH);
    delayMicroseconds(SWD_DELAY);
  }
  return out_data;
}

//1 = Write 0 = Read
void swd_turn(bool WorR)  {
  digitalWrite(swd_data_pin, HIGH);
  pinMode(swd_data_pin, INPUT_PULLUP);
  digitalWrite(swd_clock_pin, LOW);
  delayMicroseconds(SWD_DELAY);
  digitalWrite(swd_clock_pin, HIGH);
  delayMicroseconds(SWD_DELAY);
  if (WorR) { pinMode(swd_data_pin, OUTPUT);  }
  turn_state = WorR;
}
 
//16 bit

bool swd_calculate_parity16(uint16_t in_data) {
  in_data = (in_data & 0xFF) ^ (in_data >> 8);
  in_data = (in_data & 0xF) ^ (in_data >> 4);
  in_data = (in_data & 0x3) ^ (in_data >> 2);
  in_data = (in_data & 0x1) ^ (in_data >> 1);
  return in_data;
}

void swd_write16 (uint16_t in_data, uint8_t bits) {
  if (turn_state == 0) {    swd_turn(1);  }
  while (bits--)  {
    digitalWrite(swd_data_pin, in_data & 1);
    digitalWrite(swd_clock_pin, LOW);
    delayMicroseconds(SWD_DELAY);
    in_data >>= 1;
    digitalWrite(swd_clock_pin, HIGH);
    delayMicroseconds(SWD_DELAY);
  }
}

uint16_t swd_read16(uint8_t bits) {
  uint16_t out_data = 0;
  uint16_t input_bit = 1;
  if (turn_state == 1)
    swd_turn(0);
  while (bits--)  {
    if (digitalRead(swd_data_pin))    {      out_data |= input_bit;    }
    digitalWrite(swd_clock_pin, LOW);
    delayMicroseconds(SWD_DELAY);
    input_bit <<= 1;
    digitalWrite(swd_clock_pin, HIGH);
    delayMicroseconds(SWD_DELAY);
  }
  return out_data;
}

// port , ap = 1, ad = 0, read 1 write 0 // 16 bit data
bool swd_transfer16(unsigned port_address, bool APorDP, bool RorW, uint16_t &data)  {
  bool parity = APorDP ^ RorW ^ ((port_address >> 2) & 1) ^ ((port_address >> 3) & 1);
  uint8_t filled_address = (1 << 0) | (APorDP << 1) | (RorW << 2) | ((port_address & 0xC) << 1) | (parity << 5) | (1 << 7);
  swd_write(filled_address, 8);
  if (swd_read(3) == 1)    {
    if (RorW)    { // Reading 16 bits from SWD APorDP = 1
      data = swd_read(32);
      if (swd_read(1) == swd_calculate_parity(data))      {
        swd_write(0, 1);
        return true;
      }
    }
    else  { // Writing 16 bits to SWD APorDP = 0
      swd_write(data, 16);
      swd_write(swd_calculate_parity16(data), 1);
      swd_write(0, 1);
      return true;
    }
  }
   swd_write(0, 32);
  return false;
}
#define RETTY_16BIT 15
// write is 0  // DP is 0  // 16 bit data
bool swd_DP_Write16(unsigned addr, uint16_t data) {
  uint8_t retry = RETTY_16BIT;
  while (retry--)  {
    bool state = swd_transfer16(addr, 0, 0, data);
    if (state) {  return true; }
  }
  return false;
}
bool swd_DP_Read16(unsigned addr, uint16_t &data) {
  uint8_t retry = RETTY_16BIT;
  while (retry--)   {
    bool state = swd_transfer16(addr, 0, 1, data);
    if (state){ return true;}
  }
  return false;
}

bool swd_AP_Write16(unsigned addr, uint16_t data) {
  uint8_t retry = RETTY_16BIT;
  while (retry--)   {
    bool state = swd_transfer16(addr, 1, 0, data);
    if (state)  {  return true; }
  }
  return false;
}


#endif


