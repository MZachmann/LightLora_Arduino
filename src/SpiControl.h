#ifndef SPI_CONTROL
#define SPI_CONTROL

#include <SPI.h>

// SPI is inherently read/write. Write a byte always reads a byte, so...
// These methods mimic that. Both transfer methods read or write to sx127x registers
// so address = register address ( | 0x80 to write to the register)
// and value is a byte, buffers is bytes

class SpiControl
{
	public :
		SpiControl();										// empty constructor
		void Initialize(int pinSS, int pinRST, int pinINT);		// this also does LoRa control, so pass those pins in

		uint8_t transfer( uint8_t address, uint8_t value = 0);			// write a byte to address, return result
		void transfer( uint8_t address, uint8_t* buffer, uint8_t count);// write bytes to address, return values in buffer
		int getIrqPin(void);			// get the DIO0 (INT) pin number
		void initLoraPins(void);		// reset the Sx127x chip and set the pins up

	private :
		int _PinSS;		// the CS pin
		int _PinRST;	// the RST pin
		int _PinINT;	// the Interrupt pin (DIO0)
		SPISettings _Settings;	// keep our SPI settings around
};

#endif
