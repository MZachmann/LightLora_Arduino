#ifndef SPI_CONTROL
#define SPI_CONTROL

#include <SPI.h>
#include "DigitalIn.h"
#include "DigitalOut.h"

class SPISettings;

// SPI is inherently read/write. Write a byte always reads a byte, so...
// These methods mimic that. Both transfer methods read or write to sx127x registers
// so address = register address ( | 0x80 to write to the register)
// and value is a byte, buffers is bytes

class SpiControl
{
	public :
		SpiControl();										// empty constructor
		void Initialize(int pinSS, int pinRST, int pinINT);		// this also does LoRa control, so pass those pins in

		uint8_t Transfer( uint8_t address, uint8_t value = 0);			// write a byte to address, return result
		void Transfer( uint8_t address, uint8_t* buffer, uint8_t count);// write bytes to address, return values in buffer
		int GetIrqPin(void);			// get the DIO0 (INT) pin number
		void InitLoraPins(void);		// reset the Sx127x chip and set the pins up
		void EnableDirPins(uint8_t rxPin, uint8_t txPin);	// use rx,tx enable pins
		void SetSxDir(bool isReceive);

	private :
		DigitalIn _DigInt;
		DigitalOut _DigRst;
		DigitalOut _DigSS;
		DigitalOut _DigRx;
		DigitalOut _DigTx;
		SPISettings _Settings;	// keep our SPI settings around
		int _ModelNumber;		// 1276 or 1272
};

#endif
