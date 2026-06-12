#ifndef _SWD_h
#define _SWD_h
 


// #define DP_ABORT        0x00
// #define DP_IDCODE       0x00
// #define DP_CTRLSTAT     0x04
// #define DP_SELECT       0x08
// #define DP_RDBUFF       0x0c



#define SWD_DELAY       3

#if defined(ESP32)

#ifndef SWDPIN_CLK
#define SWDPIN_CLK  21   // d19 swclk
#endif

#ifndef SWDPIN_DATA
#define SWDPIN_DATA  19   // d19 miso
#endif

#endif



   void swd_gpio_init();
   uint32_t swd_init();

   bool swd_AP_Write(unsigned addr, uint32_t data);
   bool swd_AP_Read(unsigned addr, uint32_t &data);
   bool swd_DP_Write(unsigned addr, uint32_t data);
   bool swd_DP_Read(unsigned addr, uint32_t &data);
   // Однократные операции без ретраев (для неблокирующей проверки чипа)
   bool swd_DP_Read_once(unsigned addr, uint32_t &data);
   bool swd_AP_Read_once(unsigned addr, uint32_t &data);
   bool swd_transfer(unsigned port_address, bool APorDP, bool RorW, uint32_t &data);
   bool swd_calculate_parity(uint32_t in_data);
   void swd_write(uint32_t data, uint8_t bits);
   uint32_t swd_read(uint8_t bits);
   void swd_turn(bool WorR);

   bool swd_calculate_parity16(uint16_t in_data) ;
   void swd_write16 (uint16_t in_data, uint8_t bits) ;
   uint16_t swd_read16(uint8_t bits) ;

   bool swd_AP_Write16(unsigned addr, uint16_t data);

   bool swd_DP_Write16(unsigned addr, uint32_t data);
   bool swd_DP_Read16(unsigned addr, uint32_t &data) ;
   bool swd_transfer16(unsigned port_address, bool APorDP, bool RorW, uint32_t &data);





   #endif // _SWD_h


   