#ifndef LORA_UTIL
#define LORA_UTIL

#include "Sx127x.h"

class SpiControl;
class TinyVector;

// LoraUtil converts incoming data into a LoraPacket. 
// This includes rssi values as well as src,dst address
// this is really a struct of data and not a class
class LoraPacket
{
	public:
		LoraPacket();
		String msgTxt;
		uint8_t srcAddress;
		uint8_t dstAddress;
		uint8_t srcLineCount;
		uint8_t payLength;
		int rssi;
		float snr;
};

// The helper class. Construct, sendString, readPacket...
class LoraUtil : public LoraReceiver
{
	public:
		LoraUtil(int pinSS, int pinRST, int pinINT, const StringPair* params = NULL);
		LoraUtil();
		void Initialize(int pinSS, int pinRST, int pinINT, const StringPair* params);
		void SetFrequency(double newFreq);	// puts chip into standby first
		void SetFrequencyOffset(int32_t offsetFreq);
		String getError(bool doClear = false);		// for errors that happened during interrupt
		void Reset();		// reset the device
		void Sleep();		// sleep the device
		void WaitForPacket();	// go into receive mode
		// debug
		void DumpRegisters();		// dump the sx1276 registers to serial
		// send
		void sendPacket(uint8_t dstAddress, uint8_t localAddress, TinyVector& outGoing);
		void sendString(String& content);
		bool isPacketSent(bool forceClear = false);		// asynchronous transmit flag
		// receive
		LoraPacket* readPacket();
		bool isPacketAvailable();
		// these are public for use only by interrupt handler
		virtual void _doReceive(TinyVector* payload);
		virtual void _doTransmit();
	private:
		void writeInt(uint8_t value);
		//
		SpiControl* Spi();	// the SPI comm wrapper
		Sx127x* Lora();		// the Sx1276 wrapper
	private:
		int linecounter;
		LoraPacket* packet;
		volatile bool doneTransmit;

		// init spi
		SpiControl* spic;
		Sx127x* lora;
};

#endif