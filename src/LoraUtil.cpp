// this adds a little high-level init to an sx1276 and it also
//	packetizes messages with address headers. So, a bunch of higher level helper stuff.
// |	LoraUtil* _Lru = new LoraUtil();
// |	_Lru->sendString("Hello packet");
// |	while( ! _Lru->isPacketAvailable())
// |		;
// |	LoraPacket* pkt = _Lru->readPacket();
// |...print(pkt)
// MZachmann 3/2018


#include "Arduino.h"
#include "lorautil.h"
#include "TinyVector.h"
#include "sx127x.h"
#include "spicontrol.h"
#include "../Utility.h"

// -------------------------------------
// LoraPacket
// The data packet definition
// -------------------------------------
	LoraPacket::LoraPacket()
	{
		msgTxt = "";
		srcAddress = 0;
		dstAddress = 0;
		srcLineCount = 0;
		payLength = 0;
		rssi = 0;
		snr = 0;
	}

// -------------------------------------
// LoraUtil
// The high level helper
// -------------------------------------
	LoraUtil::LoraUtil(int pinSS, int pinRST, int pinINT)
	{
		this->init(pinSS, pinRST, pinINT);
	}

	// for diagnostics, get at the internal spi controller
	SpiControl* LoraUtil::Spi() 
	{
		 return spic; 
	}

	// get at the internal chip
	Sx127x* LoraUtil::Lora() 
	{
		 return lora; 
	}

	//  a LoraUtil object has an sx1276 and it can send and receive LoRa packets
	// 	sendPacket -> send a string
	// 	isPacketAvailable -> do we have a packet available?
	// 	readPacket -> get the latest packet
	void LoraUtil::init(int pinSS, int pinRST, int pinINT)
	{
		// just be neat and init variables in the __init__
		this->linecounter = 0;
		this->packet = NULL;
		this->doneTransmit = false;

		// init spi
		this->spic = new SpiControl(pinSS, pinRST, pinINT);
		this->lora = new Sx127x(NULL, this->spic);
		this->spic->initLoraPins(); // init pins and reset sx127x chip
		// init lora
		StringPair params[] = {{"tx_power_level", 5},
								{"signal_bandwidth", 125000},
								{"spreading_factor", 7},
								{"coding_rate", 5},
								{"enable_CRC", 1},
								{ StringPair::LastSP, 0}};
		this->lora->init(params);
		// pass in the callback capability
		this->lora->setReceiver(this);
		// put into receive mode and wait for an interrupt
		this->lora->receive();
	}

	String LoraUtil::getError()
	{
		// don't return the address, copy it so interrupts don't trash us
		return this->lora->lastError();
	}

	// we received a packet, deal with it
	void LoraUtil::_doReceive(TinyVector* pay)
	{
		if(this->packet)
		{
			delete this->packet;
			this->packet = NULL;
		}
		if (pay!=NULL && pay->Size() > 4)
		{
			LoraPacket* pkt = new LoraPacket();
			uint8_t* repay = pay->Data();
			pkt->srcAddress = repay[0];
			pkt->dstAddress = repay[1];
			pkt->srcLineCount = repay[2];
			pkt->payLength = repay[3];
			pkt->snr = this->lora->packetSnr();			// real snr, calced from the packetSnr value
			pkt->rssi = this->lora->packetRssi();		// this is real rssi, calced from the sx127x packetRssi value
			if(pkt->payLength > 0)
				pkt->msgTxt = (const char*)(repay+4);	// payloads are null terminated during reception
			else
				pkt->msgTxt = "";
			this->packet = pkt;
		}
	}

	// the transmit ended
	void LoraUtil::_doTransmit()
	{
		this->doneTransmit = true;
		this->lora->receive(); // wait for a packet (?)
	}

	bool LoraUtil::isPacketSent()
	{
		return this->doneTransmit;
	}

	void LoraUtil::writeInt(uint8_t value)
	{
		this->lora->writeFifo(&value, 1);
	}

	void LoraUtil::sendPacket(uint8_t dstAddress, uint8_t localAddress, TinyVector& outGoing)
	{
		// send a packet of header info and a bytearray to dstAddress
		this->linecounter = this->linecounter + 1;
		this->doneTransmit = false;
		this->lora->beginPacket();
		this->writeInt(dstAddress);				// four byte header
		this->writeInt(localAddress);
		this->writeInt(this->linecounter);
		this->writeInt(outGoing.Size());
		this->lora->writeFifo(outGoing.Data(), outGoing.Size());	// data
		this->lora->endPacket();
	}

	// send a string. use hardcoded src, dst address
	void LoraUtil::sendString(String& Content)
	{
		int l = Content.length();
		TinyVector tv(l, 1);		// leave room for the null so toCharArray is happy
		Content.toCharArray((char*)tv.Data(), l+1);
		sendPacket(0xff, 0x41, tv);	// don't send the null, though
	}

	bool LoraUtil::isPacketAvailable()
	{
		return (this->packet!=NULL) ? true : false;
	}

	// returns the current packet, which must be deleted by the caller
	LoraPacket* LoraUtil::readPacket()
	{
		// return the current packet (or none) and clear it out
		LoraPacket* pkt = this->packet;
		this->packet = NULL;
		return pkt;
	}
