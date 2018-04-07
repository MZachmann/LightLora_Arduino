#include "Arduino.h"
#include <SPI.h>
#include "SpiControl.h"

// these are my defaults for the circuit
#define PIN_ID_LORA_RESET 7
#define PIN_ID_LORA_SS 15
#define PIN_ID_LORA_DIO0 11

// Constructor - set up the pins and SPI. Pins set to -1 we use default values above.
SpiControl::SpiControl() : _Settings(5000000, MSBFIRST, SPI_MODE0)
{
}

void SpiControl::Initialize(int pinSS, int pinRST, int pinINT)
{
	SPI.begin();
	// lock out this interrupt while we are in a transaction
	SPI.usingInterrupt(digitalPinToInterrupt(PIN_ID_LORA_DIO0));

	// set the GPIO pins appropriately
	this->_PinINT =pinINT;		// the chip select(low==selected)
	pinMode(_PinINT, INPUT);	// the irq pin

	this->_PinSS = pinSS;		// the chip select(low==selected)
	pinMode(_PinSS, OUTPUT);
	digitalWrite(_PinSS, HIGH);

	this->_PinRST = pinRST;	// the reset (on high-low-high)
	pinMode(_PinRST, OUTPUT);
	digitalWrite(_PinRST, HIGH);
}

// sx127x transfer is always write two bytes while reading the second byte
// a read doesn't write the second byte. a write returns the prior value.
// write register // = 0x80 | read register //
uint8_t SpiControl::transfer( uint8_t address, uint8_t value)
{
	noInterrupts();
	SPI.beginTransaction(this->_Settings);
	digitalWrite(this->_PinSS, LOW);				// hold chip select low
	SPI.transfer(address);				// write register address
	uint8_t response = SPI.transfer(value); // write or read register walue
	digitalWrite(this->_PinSS, HIGH);
	SPI.endTransaction();
	interrupts();
	return response;
}

// transfer a set of data to/from this register. 
// On exit buffer contains the received data
void SpiControl::transfer( uint8_t address, uint8_t* buffer, uint8_t count)
{
	noInterrupts();
	SPI.beginTransaction(this->_Settings);
	digitalWrite(this->_PinSS, LOW);				// hold chip select low
	SPI.transfer(address);							// write register address
	SPI.transfer(buffer, count);			 		// r/w data values
	digitalWrite(this->_PinSS, HIGH);
	SPI.endTransaction();
	interrupts();
}

// this doesn't belong here but it doesn't really belong anywhere, so put
// it with the other loraconfig-ed stuff
int SpiControl::getIrqPin()
{
	return this->_PinINT;
}

// this doesn't belong here but it doesn't really belong anywhere, so put
// it with the other loraconfig-ed stuff
void SpiControl::initLoraPins()
{
	// initialize the pins for the LoRa device.
	digitalWrite(this->_PinSS, HIGH);
	// soft reset
	digitalWrite(this->_PinRST, HIGH);
	delay(10);
	digitalWrite(this->_PinRST, LOW);
	delay(10);
	digitalWrite(this->_PinRST, HIGH);
}
