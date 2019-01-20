#include "Arduino.h"
#include <SPI.h>
#include "SpiControl.h"
#include "TinyVector.h"

static const bool activeLowReset = true; // false for 1272, true for 1276

// Constructor - set up the pins and SPI.
SpiControl::SpiControl() : _Settings(400000, MSBFIRST, SPI_MODE0)
{
}

void SpiControl::Initialize(int pinSS, int pinRST, int pinINT)
{
	SPI.begin();
	// lock out this interrupt while we are in a transaction
	SPI.usingInterrupt(digitalPinToInterrupt(pinINT));

	// set the GPIO pins appropriately
	_DigInt.SetPin(pinINT); // establish IRQ as input
    _DigSS.SetPin(pinSS, 1); // SS is always active low
	_DigRst.SetPin(pinRST, activeLowReset ? 1 : 0);  // the reset (on high-low-high)
}

// the RF Lambda board claims to have an RX enable and TX enable pin
// and spec claims only 1,0 and 0,1 are options (?)
// testing works without the wires... so w/e
// Setup Direction Pins. Current code calls this if detects an Sx1272
void SpiControl::EnableDirPins(uint8_t rxPin, uint8_t txPin)
{
    if(rxPin != NOPIN && txPin != NOPIN)
    {
		_DigTx.SetPin(txPin, 0);
		_DigRx.SetPin(rxPin, 1);
    }
}

// Set direction pin values
void SpiControl::SetSxDir(bool isReceive)
{
	// if SetupDirPins was never called, gracefully do nothing
	if(_DigTx.IsInitialized())
	{
		_DigTx = !isReceive;
		_DigRx = isReceive;
}
}

// sx127x transfer is always write two bytes while reading the second byte
// a read doesn't write the second byte. a write returns the prior value.
// write register // = 0x80 | read register //
uint8_t SpiControl::Transfer( uint8_t address, uint8_t value)
{
uint8_t query[2];
    query[0] = address;
    query[1] = value;
	SPI.beginTransaction(this->_Settings);
	_DigSS = 0;
	SPI.transfer(query, 2);				// write register address
	_DigSS = 1;
	SPI.endTransaction();
	return query[1];
}

TinyVector tv1(25); // 20 is the standard for lru

// transfer a set of data to/from this register. 
// On exit buffer contains the received data
// we make it a single transaction for speed, otherwise the chip drops the data
void SpiControl::Transfer( uint8_t address, uint8_t* buffer, uint8_t count)
{
	SPI.beginTransaction(this->_Settings);
	_DigSS = 0;
	if(tv1.Size() < count+1)
	{
		tv1.Allocate(count+1, 5);
	}
        uint8_t* tvData = tv1.Data(); 
	tvData[0] = address;
	memcpy(tvData+1, buffer, count);
	SPI.transfer(tvData, count+1);
	memcpy(buffer, tvData+1, count);
	_DigSS = 1;
	SPI.endTransaction();
}

// this doesn't belong here but it doesn't really belong anywhere, so put
// it with the other loraconfig-ed stuff
int SpiControl::GetIrqPin()
{
	return _DigInt.GetPin();
}

// this doesn't belong here but it doesn't really belong anywhere, so put
// it with the other loraconfig-ed stuff
void SpiControl::InitLoraPins()
{
	// initialize the pins for the LoRa device.
	_DigSS = 1;
	// soft reset
	_DigRst = activeLowReset ? 1 : 0;
	delay(10);
	_DigRst = activeLowReset ? 0 : 1;
	delay(10);
	_DigRst = activeLowReset ? 1 : 0;
	delay(10);
}

