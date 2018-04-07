// This simple file sets up a feather with LoRa (internal or external)
// So that it can play ping-pong with another feather with LoRa.
// So, they send every few seconds unless..
//   if they receive a packet they send a reply packet
// So, a well-working system will play ping-pong constantly.
// One not working well will lose packets and have to start the cycle over
// MZachmann 3/2018

#include "Arduino.h"
#include "Utility.h"
#include "src/TinyVector.h"
#include "src/LoraUtil.h"

// these are for Feather RF52 board
//#define PIN_ID_LORA_RESET 7
//#define PIN_ID_LORA_SS 15
//#define PIN_ID_LORA_DIO0 11

// these are for Feather M0 with internal LoRa (which has no led1)
#define PIN_ID_LORA_SS 8
#define PIN_ID_LORA_RESET 4
#define PIN_ID_LORA_DIO0 3
#define PIN_LED1 PIN_LED

static int _TransmitPower = 5;	// power (2-17)

// did we print the error from interrupt already?
static bool _DidPrintError = false;

static int16_t packetnum = 0; // packet counter, we increment per xmission
static unsigned long _SendTime = 0;
static int _SendInterval = 6000;

// our LoraUtil static object
static LoraUtil* _Lru = NULL;

void setup()
{
	ASeries.Start(115200);
	ASeries.println("Feather Test!");

	// sit for 4 seconds in case something later crashes
	BlinkLed(true);		// show we're booting...
	for(int ii=0; ii<40; ii++)
	{
		delay(100);		// wait 4 seconds
	}
	BlinkLed(false);

	// start up the LoRa card. Optional parameters argument
	StringPair parameters[] = {{"tx_power_level", _TransmitPower},
				{"signal_bandwidth", 125000},
				{"spreading_factor", 7},
				{"coding_rate", 5},
				{"enable_CRC", 1},
				{ StringPair::LastSP, 0}};
	_Lru = new LoraUtil(PIN_ID_LORA_SS, PIN_ID_LORA_RESET, PIN_ID_LORA_DIO0, parameters);
	ASeries.println("Lora is reset");

	_SendTime = millis();
}

// if an error happened during interrupt, print and log it
void ifPrintLru(const char* prefix)
{
	String sub = _Lru->getError();
	if(sub.length() > 0)
	{
		ASeries.println(prefix + sub);
		_DidPrintError = true;
	}
}

// You may not have an LED1. Change to suit.
void BlinkLed(bool onOff)
{
	static bool pinset = false;
	if(!pinset)
	{
		pinset = true;
		pinMode(PIN_LED1, OUTPUT);
	}
	digitalWrite(PIN_LED1, onOff ? HIGH : LOW);
}


// Arduino Loop
void loop()
{
	// if we've received a packet, read it and respond to it
	if( _Lru->isPacketAvailable())
	{
		LoraPacket* pkt = _Lru->readPacket();
		ifPrintLru("Receive pkt error: ");
		if(pkt != NULL)
		{
			_SendTime = millis();
			if(pkt->msgTxt.length() > 3)
			{
				String txt = pkt->msgTxt;
				ASeries.println("Received: " + txt);
				String sending = "U" + String(pkt->rssi)+"." + String(pkt->snr) + "." + String(packetnum);
				delay(200);					// let the other side switch to receive mode
				_DidPrintError = false;		// allow print of next error
				_Lru->sendString(sending);
				ASeries.println("Sent: " + sending);
				packetnum = packetnum + 1;
			}
			delete pkt;		// we own it
		}
		else
		{
			ifPrintLru("Receiver error: ");
		}
	}
	else if((millis() - _SendTime) > _SendInterval)
	{
		// else if it has been too long, send a wake up to the other guy
		String sending = "Wake Up";
		ASeries.println("Sending: " + sending);
		_DidPrintError = false;
		_Lru->sendString(sending);
		_SendTime = millis();
	}
	else
	{
		if(!_DidPrintError)
		{
			ifPrintLru("Transmit pkt error: ");
		}
		delay(50);
	}
}