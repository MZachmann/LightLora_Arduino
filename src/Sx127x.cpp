/* This is a generic sx127x driver for the Semtech chipsets.
   In particular, it has a minor tweak for the sx1276.

This code supports interrupt driven send and receive for maximum efficiency.
Call onReceive and onTransmit to define the interrupt handlers.
	Receive handler gets a packet of data
	Transmit handler is informed the transmit ended
	Note that some functions (Serial, delay) are not valid during the interrupt call

Communications is handled by an SpiControl object wrapping SPI
There's an exact copy in Micropython at ??
*/

#include "Arduino.h"
#include "Sx127x.h"
#include "SpiControl.h"
#include "TinyVector.h"
#include "SerialWrap.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

// registers
int REG_FIFO = 0x00;
int REG_OP_MODE = 0x01;
int REG_FRF_MSB = 0x06;
int REG_FRF_MID = 0x07;
int REG_FRF_LSB = 0x08;
int REG_PA_CONFIG = 0x09;
int REG_LNA = 0x0c;
int REG_FIFO_ADDR_PTR = 0x0d;

int REG_FIFO_TX_BASE_ADDR = 0x0e;
int FifoTxBaseAddr = 0x00;
// FifoTxBaseAddr = 0x80;

int REG_FIFO_RX_BASE_ADDR = 0x0f;
int FifoRxBaseAddr = 0x00;
int REG_FIFO_RX_CURRENT_ADDR = 0x10;
int REG_IRQ_FLAGS_MASK = 0x11;
int REG_IRQ_FLAGS = 0x12;
int REG_RX_NB_BYTES = 0x13;
int REG_PKT_RSSI_VALUE = 0x1a;
int REG_PKT_SNR_VALUE = 0x1b;
int REG_MODEM_CONFIG_1 = 0x1d;
int REG_MODEM_CONFIG_2 = 0x1e;
int REG_PREAMBLE_MSB = 0x20;
int REG_PREAMBLE_LSB = 0x21;
int REG_PAYLOAD_LENGTH = 0x22;
int REG_FIFO_RX_BYTE_ADDR = 0x25;
int REG_MODEM_CONFIG_3 = 0x26;
int REG_RSSI_WIDEBAND = 0x2c;
int REG_DETECTION_OPTIMIZE = 0x31;
int REG_DETECTION_THRESHOLD = 0x37;
int REG_SYNC_WORD = 0x39;
int REG_DIO_MAPPING_1 = 0x40;
int REG_VERSION = 0x42;

// modes
int MODE_LONG_RANGE_MODE = 0x80;  // bit 7: 1 => LoRa mode
int MODE_SLEEP = 0x00;
int MODE_STDBY = 0x01;
int MODE_TX = 0x03;
int MODE_RX_CONTINUOUS = 0x05;
// MODE_RX_SINGLE = 0x06
// 6 is not supported on the 1276
int MODE_RX_SINGLE = 0x05;
 
// PA config
int PA_BOOST = 0x80;
 
// IRQ masks
int IRQ_TX_DONE_MASK = 0x08;
int IRQ_PAYLOAD_CRC_ERROR_MASK = 0x20;
int IRQ_RX_DONE_MASK = 0x40;
int IRQ_RX_TIME_OUT_MASK = 0x80;
 
// Buffer size
int MAX_PKT_LENGTH = 255;

static volatile bool IsRunning = false;

// pass in non-default parameters for any/all options in the constructor parameters argument
StringPair DEFAULT_PARAMETERS[] = {{"frequency", 915}, {"tx_power_level", 2}, 
									{"signal_bandwidth", 125000}, {"spreading_factor", 7},
									{ "coding_rate", 5}, {"preamble_length", 8},
					  				{"implicitHeader", 0}, {"sync_word", 0x12}, {"enable_CRC", 0},
									{ StringPair::LastSP, 0}};

int REQUIRED_VERSION = 0x12;

// our singleton for interrupt usage
static Sx127x* _Singleton = NULL;

// --------------------------------------------------------------------
// StringPairs let us create the equivalent of a Python dictionary
// --------------------------------------------------------------------
	StringPair::StringPair(String name, int value) 
	{
		Name = name;
		Value = value; 
	}

	// a marker for end of list so we don't pass in counts
	String StringPair::LastSP("LXXL");

	// look for a string in the string pairs
	int IndexOfPair(StringPair* dict, String& value)
	{
		if(dict == NULL)
		{
			return -1;
		}

		int i = 0;
		while( dict[i].Name != StringPair::LastSP)
		{
			if(dict[i].Name == value)
				return i;
			i++;
		}
		return -1;
	}

// --------------------------------------------------------------------
// Sx127x is low-level Semtech SX127x chip support
// --------------------------------------------------------------------
	// destructor, get rid of local allocations
	Sx127x::~Sx127x()
	{
		if(_Singleton == this)
		{
			_Singleton = NULL;
		}
		if(_SpiControl != NULL)
		{
			delete _SpiControl;
			_SpiControl = NULL;
		}
		if(_FifoBuf != NULL)
		{
			delete _FifoBuf;
			_FifoBuf = NULL;
		}
	}

	/// Standard SX127x library. Requires an spicontrol.SpiControl instance for spiControl
	Sx127x::Sx127x(String* name, SpiControl* spic) : _FifoBuf(NULL), _SpiControl(NULL), _LoraRcv(NULL)
	{
		this->_Lock = true;
		this->_Name = (name != NULL) ? (*name) : "Sx127x";
		this->_SpiControl = spic;   	// the spi wrapper - see spicontrol.py
		this->_IrqPin = spic->getIrqPin(); // a way to need loracontrol only in spicontrol
		this->_FifoBuf = new TinyVector(0, 30);	// our sorta persistent buffer
		_Singleton = this;				// yuck... but required for interrupt handler
		ASeries.println("Finish sx127x construction.");
	}

	// if we passed in a param use it, else use default
	int UseParam(StringPair* parameters, const char* whom)
	{
		String who = whom;
		int idx = IndexOfPair(parameters, who);
		if(-1 == idx)
		{
			idx = IndexOfPair(DEFAULT_PARAMETERS, who);
			if(-1 != idx)
			{
				return DEFAULT_PARAMETERS[idx].Value;
			}
			// ! error
			return 0;
		}
		return parameters[idx].Value;
	}

	bool Sx127x::init(StringPair* params)
	{
		// check version
		ASeries.println("Reading version");
		int version = this->readRegister(REG_VERSION);
		if (version != REQUIRED_VERSION)
		{
			ASeries.println("Detected version:" + String(version));
			return false;
		}
		ASeries.println("Read version ok");

		// put in LoRa and sleep mode
		this->sleep();
		ASeries.println("Sleeping");

		// config
		int freq = UseParam(params, "frequency");	// get frequency as int in MHz
		this->setFrequency((double)freq * 1E6);
		this->setSignalBandwidth(UseParam(params, "signal_bandwidth"));

		// set LNA boost
		this->writeRegister(REG_LNA, this->readRegister(REG_LNA) | 0x03);

		// set auto AGC
		this->writeRegister(REG_MODEM_CONFIG_3, 0x04);

		this->setTxPower(UseParam(params, "tx_power_level"));
		this->_ImplicitHeaderMode = false;
		this->implicitHeaderMode(UseParam(params, "implicitHeader"));
		this->setSpreadingFactor(UseParam(params, "spreading_factor"));
		this->setCodingRate(UseParam(params, "coding_rate"));
		this->setPreambleLength(UseParam(params, "preamble_length"));
		this->setSyncWord(UseParam(params, "sync_word"));
		this->enableCRC(UseParam(params, "enable_CRC"));

		// set base addresses
		this->writeRegister(REG_FIFO_TX_BASE_ADDR, FifoTxBaseAddr);
		this->writeRegister(REG_FIFO_RX_BASE_ADDR, FifoRxBaseAddr);

		this->standby();
		ASeries.println("Finish sx127x initialization.");
		return true;
	}

	// get the last error message if there was one during interrupt
	const String& Sx127x::lastError()
	{
		return this->_LastError;
	}

	// start sending a packet (reset the fifo address, go into standby)
	void Sx127x::beginPacket(bool implicitHeaderMode)
	{
		this->standby();
		this->implicitHeaderMode(implicitHeaderMode);
		// reset FIFO address and paload length
		this->writeRegister(REG_FIFO_ADDR_PTR, FifoTxBaseAddr);
		this->writeRegister(REG_PAYLOAD_LENGTH, 0);
	}

	// finished putting packet into fifo, send it
	// non-blocking so don't immediately receive or this will fail...
	// Wait for the transmit-done interrupt
	void Sx127x::endPacket() 
	{
		// non-blocking end packet
		if (this->_LoraRcv)
		{
		   // enable tx to raise DIO0
			this->PrepIrqHandler(Sx127x::HandleOnTransmit);		   // attach handler
			this->writeRegister(REG_DIO_MAPPING_1, 0x40);		   // enable transmit dio0
		}
		else
		{
			this->PrepIrqHandler(NULL);							// no handler
		}
		// put in TX mode
		this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);
	}

	// synchronous call to see if transmit is done
	// this clear IRQ so only returns true once
	bool Sx127x::isTxDone() 
	{
		// if Tx is done return true, and clear irq register - so it only returns true once 
		if(this->_LoraRcv)
		{
			ASeries.println("Do not call isTxDone with transmit interrupts enabled. Use the callback.");
			return false;
		}
		int irqFlags = this->getIrqFlags();
		if ((irqFlags & IRQ_TX_DONE_MASK) == 0)
		{
			return false;
		}
		return true;
	}

	// write a buffer contents to the Fifo in prep for sending
	int Sx127x::writeFifo(const uint8_t* buffer, int size)
	{
		uint8_t currentLength = this->readRegister(REG_PAYLOAD_LENGTH);
		// check size
		size = min(size, (MAX_PKT_LENGTH - FifoTxBaseAddr - currentLength));
		if(size == 1)
		{
			uint8_t value = *buffer;
			this->_SpiControl->transfer(REG_FIFO | 0x80, value);
		}
		else
		{
			// copy the data to a temp buffer since it gets overwritten
			this->_FifoBuf->Allocate(size);
			uint8_t* udata = this->_FifoBuf->Data();
			memcpy(udata, buffer, size);
			// now
			this->_SpiControl->transfer(REG_FIFO | 0x80, udata, size);
		}
		// update length
		this->writeRegister(REG_PAYLOAD_LENGTH, currentLength + size);
		return size;
	}

	// get a thread lock (or whatever)
	void Sx127x::acquire_lock(bool lock)
	{
		if(this->_Lock)
		{
			if (lock)
			{
				while(IsRunning)
				{
					delay(20);
					//Serial.println("Waiting for unlock");
				}
				IsRunning = true;
				//_thread.lock();
			}
			else
			{
				IsRunning = false;
				//_thread.unlock();
			}
		}
	}

	// read/clear the IRQ flags. returns pre-clear value
	uint8_t Sx127x::getIrqFlags() 
	{
		// get and reset the irq register
		int irqFlags = this->readRegister(REG_IRQ_FLAGS);
		this->writeRegister(REG_IRQ_FLAGS, irqFlags);	// clear IRQs
		return irqFlags;
	}

	// this returns real (not packet) rssi
	int Sx127x::packetRssi() 
	{
		int rssi = this->readRegister(REG_PKT_RSSI_VALUE);
		// Adjust the RSSI, datasheet page 87. This maximizes accuracy
		float snr = packetSnr();
		if(this->_Frequency < 868)
		{	// 433MHz
			rssi = rssi - 164;
			if(snr < 0)
			{
				rssi += snr;		// decrease it
			}
		}
		else
		{	// 868,915MHz
			if(snr < 0)
			{
				rssi = rssi + snr - 157;
			}
			else
			{
				rssi = (int)(rssi * 16 / 15) - 157;
			}
		}
		return rssi;
	}

	// real SNR
	float Sx127x::packetSnr() 
	{
		return ((int8_t)this->readRegister(REG_PKT_SNR_VALUE)) * 0.25;	// signed result
	}

	// go into standby mode. preparatory to sending usually
	void Sx127x::standby() 
	{
		this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
	}

	// sleep the chip. it auto-wakes up but more slowly than if wide awake
	void Sx127x::sleep() 
	{
		this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
	}

	// valid power levels are 0...14 if from normal outputPin
	// and 2...17 if PA_BOOST
	void Sx127x::setTxPower(int level, int outputPin)
	{
		if (outputPin == PA_OUTPUT_RFO_PIN)
		{
			// RFO
			if(level < 0)
			{
				level = 0;
			}
			else if(level > 14)
			{
				level = 14;
			}
			this->writeRegister(REG_PA_CONFIG, 0x70 | level);
		}
		else
		{
			// PA BOOST
			if(level < 2)
			{
				level = 2;
			}
			else if(level > 17)
			{
				level = 17;
			}
			this->writeRegister(REG_PA_CONFIG, PA_BOOST | (level - 2));
		}
	}

	// set the frequency band. passed in MHz
	// Frf register setting = Freq / FSTEP where
	// FSTEP = FXOSC/2**19 where FXOSC=32MHz. So FSTEP==61.03515625
	void Sx127x::setFrequency(double frequency)
	{
		ASeries.println("Set frequency to: " + String(frequency));
		this->_Frequency = frequency;
		uint32_t stepf = (uint32_t)(frequency / 61.03515625);	// get 24 bits of freq/step
		uint8_t frfs[3];
		frfs[0] = 0xff & (stepf>>16);
		frfs[1] = 0xff & (stepf>>8);
		frfs[2] = 0xff & (stepf);
		ASeries.println("Frf registers: " + String(frfs[0]) + "." + String(frfs[1]) + "." + String(frfs[2]));
		this->writeRegister(REG_FRF_MSB, frfs[0]);
		this->writeRegister(REG_FRF_MID, frfs[1]);
		this->writeRegister(REG_FRF_LSB, frfs[2]);
	}

	void Sx127x::setSpreadingFactor(int sf)
	{
		ASeries.println("Set spreading factor to: " + String(sf));
		sf = min(max(sf, 6), 12);
		this->writeRegister(REG_DETECTION_OPTIMIZE, (sf == 6) ? 0xc5 : 0xc3);
		this->writeRegister(REG_DETECTION_THRESHOLD, (sf == 6) ? 0x0c : 0x0a);
		this->writeRegister(REG_MODEM_CONFIG_2, (this->readRegister(REG_MODEM_CONFIG_2) & 0x0f) | ((sf << 4) & 0xf0));
	}

	void Sx127x::setSignalBandwidth(int sbw)
	{
		ASeries.println("Set sbw to: " + String(sbw));
		int bins[] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000};
		int bw = 9;
		for (int i=0; i<ARRAY_SIZE(bins); i++)
		{
			if (sbw <= bins[i])
			{
				bw = i;
				break;
			}
		}
		this->writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0x0f) | (bw << 4));
	}

	void Sx127x::setCodingRate(int denominator)
	{
		ASeries.println("Set coding rate to: " + String(denominator));
		// this takes a value of 5..8 as the denominator of 4/5, 4/6, 4/7, 5/8
		denominator = min(max(denominator, 5), 8);
		int cr = denominator - 4;
		this->writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0xf1) | (cr << 1));
	}

	void Sx127x::setPreambleLength(int length)
	{
		this->writeRegister(REG_PREAMBLE_MSB, (length >> 8) & 0xff);
		this->writeRegister(REG_PREAMBLE_LSB, (length >> 0) & 0xff);
	}

	void Sx127x::enableCRC(bool enable_CRC)
	{
		ASeries.println("Enable crc: " + (enable_CRC ? String("Yes") : String("No")));
		uint8_t modem_config_2 = this->readRegister(REG_MODEM_CONFIG_2);
		uint8_t config = enable_CRC ? (modem_config_2 | 0x04) : (modem_config_2 & 0xfb);
		this->writeRegister(REG_MODEM_CONFIG_2, config);
	}

	void Sx127x::setSyncWord(int sw)
	{
		this->writeRegister(REG_SYNC_WORD, sw);
	}

	void Sx127x::implicitHeaderMode(bool implicitHeaderMode)
	{
		if (this->_ImplicitHeaderMode != implicitHeaderMode)  // set value only if different.
		{
			ASeries.println("Set implicit header: " + (implicitHeaderMode ? String("Yes") : String("No")));
			this->_ImplicitHeaderMode = implicitHeaderMode;
			uint8_t modem_config_1 = this->readRegister(REG_MODEM_CONFIG_1);
			uint8_t config = implicitHeaderMode ? (modem_config_1 | 0x01) : (modem_config_1 & 0xfe);
			this->writeRegister(REG_MODEM_CONFIG_1, config);
		}
	}

	void Sx127x::PrepIrqHandler(InterruptFn handlefn)
	{
		// attach the handler to the irq pin, disable if None
		if (this->_IrqPin != 0)
		{
			if (handlefn)
			{
				attachInterrupt(digitalPinToInterrupt(this->_IrqPin), handlefn, RISING);
			}
			else
			{
				detachInterrupt(digitalPinToInterrupt(this->_IrqPin));
			}
		}
	}

	void Sx127x::setReceiver(LoraReceiver* receiver)
	{
		this->_LoraRcv = receiver;
	}

	// enable reception. Place an interrupt handler and tell Lora chip to mode RX.
	void Sx127x::receive(int size)
	{
		this->implicitHeaderMode(size > 0);
		if (size > 0)
		{
			this->writeRegister(REG_PAYLOAD_LENGTH, size & 0xff);
		}
		// enable rx to raise DIO0
		if (this->_LoraRcv)
		{
			this->PrepIrqHandler(Sx127x::HandleOnReceive);			// attach handler
			this->writeRegister(REG_DIO_MAPPING_1, 0x00);
		}
		else
		{
			this->PrepIrqHandler(NULL);							// no handler
		}
		// The last packet always starts at FIFO_RX_CURRENT_ADDR
		// no need to reset FIFO_ADDR_PTR
		this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
	}

	// called by the static receive interrupt handler
	// not reentrant
	void Sx127x::ReceiveSub()
	{
	static TinyVector payload(0,40);		// allocate here, not in the interrupt

		this->_LastError = "";
		this->acquire_lock(true);			// lock until TX_Done
		uint8_t irqFlags = this->getIrqFlags();
		uint8_t irqbad = IRQ_PAYLOAD_CRC_ERROR_MASK | IRQ_RX_TIME_OUT_MASK;
		if ( (irqFlags & IRQ_RX_DONE_MASK) &&
		     (irqFlags & irqbad) == 0 &&
			 (this->_LoraRcv != NULL) )
			{
				// it's a receive data ready interrupt
				this->ReadPayload(payload);
				this->acquire_lock(false);	 // unlock when done reading
				this->_LoraRcv->_doReceive(&payload);
			}
		else
		{
			this->acquire_lock(false);			 // unlock in any case.
			if (!(irqFlags & IRQ_RX_DONE_MASK))
			{
				this->_LastError = "not rx done mask";
			}
			else if((irqFlags & irqbad) != 0)
			{
				if(irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK)
					this->_LastError = "rx crc error";
				else
					this->_LastError = "rx timeout error";
			}
			else
			{
				this->_LastError = "no receive method defined";
			}
		}
	}

	// got a receive interrupt, handle it
	// a static method to receive the interrupt, so this uses _Singleton to call an intance method
	void Sx127x::HandleOnReceive(void)
	{
		if(_Singleton)
			_Singleton->ReceiveSub();
	}

	// called by the static transmit interrupt handler
	void Sx127x::TransmitSub(void)
	{
		this->_LastError = "";
		this->acquire_lock(true);			  // lock until flags cleared
		int irqFlags = this->getIrqFlags();
		this->acquire_lock(false);			 // unlock
		if (irqFlags & IRQ_TX_DONE_MASK)
		{
			// it's a transmit finish interrupt
			this->PrepIrqHandler(NULL);	   // disable handler since we're done
			if (this->_LoraRcv)
			{
				this->_LoraRcv->_doTransmit();
			}
			else
			{
				this->_LastError = "transmit callback but no callback method";
			}
		}
		else
		{
			this->_LastError = "transmit callback but not txdone: " + String(irqFlags);
		}
	}

	// a static method to receive the interrupt, so this uses _Singleton to call an intance method
	void Sx127x::HandleOnTransmit(void)
	{
		if(_Singleton)
			_Singleton->TransmitSub();
	}

	// check to see if we have a received packet pending (synchronous)
	bool Sx127x::receivedPacket(int size)
	{
		// when no receive handler, this tells if packet ready. Preps for receive
		if (this->_LoraRcv)
		{
			ASeries.println("Do not call receivedPacket. Use the callback.");
			return false;
		}
		int irqFlags = this->getIrqFlags();
		this->implicitHeaderMode(size > 0);
		if (size > 0)
		{
			this->writeRegister(REG_PAYLOAD_LENGTH, size & 0xff);
		}
		// if (irqFlags & IRQ_RX_DONE_MASK) and \
		   // (irqFlags & IRQ_RX_TIME_OUT_MASK == 0) and \
		   // (irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK == 0):
		if (irqFlags == IRQ_RX_DONE_MASK)  // RX_DONE only, irqFlags should be 0x40
		{
			// automatically standby when RX_DONE
			return true;
		}
		else if (this->readRegister(REG_OP_MODE) != (MODE_LONG_RANGE_MODE | MODE_RX_SINGLE))
		{
			// no packet received and not in receive mode
			// reset FIFO address / // enter single RX mode
			this->writeRegister(REG_FIFO_ADDR_PTR, FifoRxBaseAddr);
			this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_SINGLE);
		}
		return false;
	}

	// read the input packet from the Fifo
	void Sx127x::ReadPayload(TinyVector& tv) 
	{
		// set FIFO address to current RX address
		// fifo_rx_current_addr = this->readRegister(REG_FIFO_RX_CURRENT_ADDR)
		this->writeRegister(REG_FIFO_ADDR_PTR, this->readRegister(REG_FIFO_RX_CURRENT_ADDR));
		// read packet length
		uint8_t packetLength = this->_ImplicitHeaderMode ? this->readRegister(REG_PAYLOAD_LENGTH) :	this->readRegister(REG_RX_NB_BYTES);
		tv.Allocate(packetLength, 1);		// one extra for the null. hopefully this does not reallocate
		this->_SpiControl->transfer(REG_FIFO, tv.Data(), packetLength);	// get all data in one spi call
		// do not use tv[packetLength] here because if the allocate moves the Data then
		// the optimizer uses the wrong pointer...
		tv.Data()[packetLength] = 0;				// null terminate any strings
	}

	uint8_t Sx127x::readRegister(uint8_t address)
	{
		uint8_t response = this->_SpiControl->transfer(address & 0x7f);
		return response;
	}

	void Sx127x::writeRegister(uint8_t address, uint8_t value)
	{
		this->_SpiControl->transfer(address | 0x80, value);
	}

	void Sx127x::dumpRegisters() 
	{
		for(int i=0; i<128; i++)
		{
			ASeries.println( String(i) + ":" + String(this->readRegister(i)));
				// "0x{0:02x}: {1:02x}".format(i, this->readRegister(i)));
		}
	}

