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
int REG_FRF_MSB = 0x06;	// frequency setting
int REG_FRF_MID = 0x07;
int REG_FRF_LSB = 0x08;
int REG_PA_CONFIG = 0x09;
int REG_OCP = 0x0b;	// overcurrent
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
int REG_IMAGE_CAL = 0x3b;
int REG_TEMP = 0x3c;		// temperature probe
int REG_DIO_MAPPING_1 = 0x40;
int REG_VERSION = 0x42;
int REG_PA_DAC = 0x4d;

// modes
int MODE_LONG_RANGE_MODE = 0x80;  // Bitfield -> bit 7: 1 => LoRa mode
//-
int MODE_SLEEP = 0x00;
int MODE_STDBY = 0x01;
int MODE_TX = 0x03;
int MODE_RX_CONTINUOUS = 0x05;
// MODE_RX_SINGLE = 0x06
// 6 is not supported on the 1276
int MODE_RX_SINGLE = 0x06;
 
// fsk modes for calibration setting
int MODE_SYNTHESIZER_TX = 0x02;
int MODE_TRANSMITTER = 0x03;
int MODE_SYNTHESIZER_RX = 0x04;
int MODE_RECEIVER = 0x05;

// calibration fields
int IMAGECAL_AUTOIMAGECAL_MASK = 0x7F;
int IMAGECAL_AUTOIMAGECAL_ON = 0x80;
int IMAGECAL_AUTOIMAGECAL_OFF = 0x00; // Default
int IMAGECAL_IMAGECAL_MASK = 0xBF;
int IMAGECAL_IMAGECAL_START = 0x40;
int IMAGECAL_IMAGECAL_RUNNING = 0x20;
int IMAGECAL_TEMPTHRESHOLD_MASK = 0xF9;
int IMAGECAL_TEMPMONITOR_MASK = 0xFE;
int IMAGECAL_TEMPMONITOR_ON = 0x00; // Default
int IMAGECAL_TEMPMONITOR_OFF = 0x01;

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
static const StringPair DEFAULT_PARAMETERS[] = {{"frequency", 915}, {"frequency_low",0},
									{"tx_power_level", 2}, 
									{"signal_bandwidth", 125000}, {"spreading_factor", 7},
									{ "coding_rate", 5}, {"preamble_length", 8},
									{"freq_offset", 0},
					  				{"implicitHeader", 0}, {"sync_word", 0x12}, {"enable_CRC", 0},
									{"power_pin", PA_OUTPUT_PA_BOOST_PIN},
									{ StringPair_LastSP, 0}};

int REQUIRED_VERSION = 0x12;
int REQUIRED_VERSION_1272 = 0x22;

// our singleton for interrupt usage
static Sx127x* _Singleton = NULL;
static const bool _ActiveLowIrq = false;

// --------------------------------------------------------------------
// StringPairs let us create the equivalent of a Python dictionary
// --------------------------------------------------------------------

	// look for a string in the string pairs
	int IndexOfPair(const StringPair* dict, const char* value)
	{
		if(dict == NULL)
		{
			return -1;
		}

		int i = 0;
		while(0 != strcmp(dict[i].Name, StringPair_LastSP))
		{
			if(0 == strcmp(dict[i].Name, value))
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
		if(_FifoBuf != NULL)
		{
			delete _FifoBuf;
			_FifoBuf = NULL;
		}
	}

	/// Standard SX127x library. Requires an spicontrol.SpiControl instance for spiControl
	Sx127x::Sx127x() : _FifoBuf(NULL), _SpiControl(NULL), _LoraRcv(NULL), _LastSentTime(0), _LastReceivedTime(0), _IrqFunction(nullptr), _IrqPin(-1)
	{

	}

	void Sx127x::Initialize(String* name, SpiControl* spic, uint8_t rxPin, uint8_t txPin)
	{
		this->_Lock = true;
		this->_Name = (name != NULL) ? (*name) : "Sx127x";
		this->_SpiControl = spic;   	// the spi wrapper - see spicontrol.py
		this->_IrqPin = spic->GetIrqPin(); // a way to need loracontrol only in spicontrol
		this->_FifoBuf = new TinyVector(0, 30);	// our sorta persistent buffer
		this->_LastSentTime = 0;
		this->_LastReceivedTime = 0;
		_Singleton = this;				// yuck... but required for interrupt handler
		this->PrepIrqHandler(Sx127x::HandleInterrupt);		// call this once to set the interrupt handler
		ASeries.println("Finish Sx127x construction.");
		if(Is1272())
		{
			ASeries.printf("Setting up direction pins with %d . %d", rxPin, txPin);
			this->_SpiControl->EnableDirPins(rxPin, txPin);    // use the rxenable and txenable pins
	}
	}

	// if we passed in a param use it, else use default
	int32_t UseParam(const StringPair* parameters, const char* whom)
	{
		int idx = IndexOfPair(parameters, whom);
		if(-1 == idx)
		{
			idx = IndexOfPair(DEFAULT_PARAMETERS, whom);
			if(-1 != idx)
			{
				return DEFAULT_PARAMETERS[idx].Value;
			}
			// ! error
			return 0;
		}
		return parameters[idx].Value;
	}

	bool Sx127x::init(const StringPair* params)
	{
		// check version
		ASeries.println("Reading version");
		int version = this->readRegister(REG_VERSION);
		if(version == REQUIRED_VERSION)
		{
			_ModelNumber = 1276;	// sx1276, hopeRF
		}
		else if(version == REQUIRED_VERSION_1272)
		{
			_ModelNumber = 1272;	// sx1272
		}
		else
		{
			ASeries.printf("Detected incorrect version: %d", version);
			return false;
		}
		ASeries.printf("Read version %d ok", _ModelNumber);

		// put in LoRa and sleep mode
		this->sleep();
		ASeries.println("Sleeping");

		// config set frequency offset before setting frequency
		double freqOff = UseParam(params, "freq_offset");
		this->setFrequencyOffset(freqOff);

		double freq = 1E6 * (double)UseParam(params, "frequency");	// get frequency as int in MHz
		double freqHz = (double)UseParam(params, "frequency_low");	// any remaining 0...999,999 Hz
		this->setFrequency(freq + freqHz);

		// set auto AGC for LNA gain. do this before setting bandwidth,spreading factor
		// since they set the low-data-rate flag bit in the same register
		if(!Is1272())
			this->writeRegister(REG_MODEM_CONFIG_3, 0x04);

		this->setSignalBandwidth(UseParam(params, "signal_bandwidth"));

		// set LNA boost ???
		this->writeRegister(REG_LNA, this->readRegister(REG_LNA) | 0x03);

		int powerpin = UseParam(params, "power_pin");	// powerpin = PA_OUTPUT_PA_BOOST_PIN or PA_OUTPUT_RFO_PIN
		if(powerpin != PA_OUTPUT_PA_BOOST_PIN && powerpin != PA_OUTPUT_RFO_PIN)
		{
			ASeries.printf("Invalid power_pin setting. Must be 0 or 1. It is = %d", powerpin);
			powerpin = PA_OUTPUT_PA_BOOST_PIN; // ?
		}
		this->setTxPower(UseParam(params, "tx_power_level"), powerpin);
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

	void Sx127x::clearLastError()
	{
		this->_LastError = "";
	}

	// start sending a packet (reset the fifo address, go into standby)
	void Sx127x::beginPacket(bool implicitHeaderMode)
	{
		_SpiControl->SetSxDir(false);	// turn on transmit rf chain
		_IrqFunction = nullptr;	// this isn't necessary but if things go wrong it helps with debug
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
			_IrqFunction = &Sx127x::TransmitSub;
			this->writeRegister(REG_DIO_MAPPING_1, 0x40);		   // enable transmit dio0
		}
		else
		{
			_IrqFunction = nullptr;
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
			this->_SpiControl->Transfer(REG_FIFO | 0x80, value);
		}
		else
		{
			// copy the data to a temp buffer since it gets overwritten
			this->_FifoBuf->Allocate(size);
			uint8_t* udata = this->_FifoBuf->Data();
			memcpy(udata, buffer, size);
			// now
			this->_SpiControl->Transfer(REG_FIFO | 0x80, udata, size);
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

	uint32_t Sx127x::getLastReceivedTime(void)
	{
		return this->_LastReceivedTime;
	}

	uint32_t Sx127x::getLastSentTime(void)
	{
		return this->_LastSentTime;
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

	// set output power in dBm
	// valid power levels are 0...14 if from normal outputPin
	// and 2...20 if PA_BOOST
	// Supply current in Transmit mode with impedance matching	
	// RFOP = +20 dBm, on PA_BOOST  -- 120mA
	// **RFOP = +17 dBm, on PA_BOOST -- 87mA
	// RFOP = +13 dBm, on RFO_LF/HF pin -- 29mA
	// RFOP = + 7 dBm, on RFO_LF/HF pin	-- 20mA
	void Sx127x::setTxPower(int level, int outputPin)
	{
		ASeries.printf("Set transmit power to: %d at pin: %d", level, outputPin);

		// I think the boosted system is power-limited by default
		// so if boosted, bump the power max in the RegPaDac
		if(outputPin == PA_OUTPUT_PA_BOOST_PIN)
		{
			if(level > 17)
			{
				uint8_t dacSet = readRegister(REG_PA_DAC);	// retain existing upper bits
				uint8_t newDac = dacSet | 7;				// allow pa up to 20dBm
				ASeries.printf("Set PaDac value from %d to %d", (int)dacSet, (int)newDac);
				writeRegister(REG_PA_DAC, newDac);

				// increase overcurrent max - requires short duty cycle
				uint8_t newOcp = 0x20 + 18;		// 150mA [-30 + 10*value]
				ASeries.printf("Increasing allowed current to 150mA");
				writeRegister(REG_OCP, newOcp);
			}
			else
			{
				uint8_t dacSet = readRegister(REG_PA_DAC);	// retain existing upper bits
				uint8_t newDac = (dacSet & ~7) | 4;			// do not allow 20dBm
				ASeries.printf("Set Dac value from %d to %d", (int)dacSet, (int)newDac);
				writeRegister(REG_PA_DAC, newDac);

				// set default overcurrent max
				uint8_t newOcp = 11;		// 100mA [45 + 5*value]
				ASeries.printf("Setting allowed current to 100mA");
				writeRegister(REG_OCP, newOcp);
			}
		}

		if (outputPin == PA_OUTPUT_RFO_PIN)
		{
			// RFO pin. Max of +14dBm
			if(level < 0)
			{
				level = 0;
			}
			else if(level > 14)
			{
				level = 14;
			}
			this->writeRegister(REG_PA_CONFIG, 0x70 | level);	// set Pmax=15dBm and Pout=PaConfig[0:4]=level
		}
		else
		{
			// PA BOOST pin
			if(level < 2)
			{
				level = 2;
			}
			else if(level > 20)
			{
				level = 20;
			}
			// normalize to 0...15
			if( level > 17)
			{
				level = level - 5;		// above 17 adds (?) +3 boost in the REG_PA_DAC. Pout=5+PaConfig[0:4]
			}
			else
			{
				level = level - 2;		// <= 17 and Pout=2+PaConfig[0:4] (dBm)
			}
			this->writeRegister(REG_PA_CONFIG, PA_BOOST | level);	// per spec the 0x70 bits are ignored in boost mode
		}
	}

	// set the frequency band. passed in Hz
	// Frf register setting = Freq / FSTEP where
	// FSTEP = FXOSC/2**19 where FXOSC=32MHz. So FSTEP==61.03515625
	void Sx127x::setFrequency(double frequency)
	{
		ASeries.printf("Set frequency to: %12g with offset %g", frequency, _FrequencyOffset);
		this->_Frequency = frequency;
		uint32_t stepf = (uint32_t)((frequency+_FrequencyOffset) / 61.03515625);	// get 24 bits of freq/step
		uint8_t frfs[3];
		frfs[0] = 0xff & (stepf>>16);
		frfs[1] = 0xff & (stepf>>8);
		frfs[2] = 0xff & (stepf);
		ASeries.printf("Frf registers: %d.%d.%d", (int)frfs[0], (int)frfs[1], (int)frfs[2]);
		this->writeRegister(REG_FRF_MSB, frfs[0]);
		this->writeRegister(REG_FRF_MID, frfs[1]);
		this->writeRegister(REG_FRF_LSB, frfs[2]);
	}

	// this is a simple way to adjust for crystal inaccuracy
	// set this and it's offset as a constant to the frequency settings
	// (optional)
	void Sx127x::setFrequencyOffset(double offset)
	{
		ASeries.printf("Set frequency offset to: %g", offset);
		_FrequencyOffset = offset;
		if(_Frequency != 0)
		{
			setFrequency(_Frequency);
		}
	}

	void Sx127x::setSpreadingFactor(int sf)
	{
		ASeries.printf("Set spreading factor to: %d", sf);
		sf = min(max(sf, 6), 12);
		_SpreadingFactor = sf;
		this->writeRegister(REG_DETECTION_OPTIMIZE, (sf == 6) ? 0xc5 : 0xc3);
		this->writeRegister(REG_DETECTION_THRESHOLD, (sf == 6) ? 0x0c : 0x0a);
		this->writeRegister(REG_MODEM_CONFIG_2, (this->readRegister(REG_MODEM_CONFIG_2) & 0x0f) | ((sf << 4) & 0xf0));
		setLowDataRate();		// set the low-data-rate flag
	}

	void Sx127x::setSignalBandwidth(int sbw)
	{
		ASeries.printf("Set sbw to: %d", sbw);
		int bins[] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};
		int bw = 9;		// default to 500K
		for (int i=0; i<ARRAY_SIZE(bins); i++)
		{
			if (sbw <= bins[i])
			{
				bw = i;
				break;
			}
		}
		_SignalBandwidth = bins[bw];
		if(Is1272())
		{
			// only supports 125,250,500
			if(bw < 7)
			{
				bw = 7;
				ASeries.printf("Unable to set low data rate of %d for Sx1272", sbw);
			}
			bw -= 7;
			writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0x3f) | (bw << 6));
		}
		else
			writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0x0f) | (bw << 4));
		setLowDataRate();		// set the low-data-rate flag
	}

	void Sx127x::setCodingRate(int denominator)
	{
		ASeries.printf("Set coding rate to: %d", denominator);
		// this takes a value of 5..8 as the denominator of 4/5, 4/6, 4/7, 5/8
		denominator = min(max(denominator, 5), 8);
		int cr = denominator - 4;
		if(Is1272())
		{
			this->writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0xC7) | (cr << 3));
		}
		else
		{
		this->writeRegister(REG_MODEM_CONFIG_1, (this->readRegister(REG_MODEM_CONFIG_1) & 0xf1) | (cr << 1));
		}
	}

	void Sx127x::setPreambleLength(int length)
	{
		ASeries.printf("Set preamble length to: %d", length);
		this->writeRegister(REG_PREAMBLE_MSB, (length >> 8) & 0xff);
		this->writeRegister(REG_PREAMBLE_LSB, (length >> 0) & 0xff);
	}

	void Sx127x::enableCRC(bool enable_CRC)
	{
		ASeries.printf("Enable crc: %s", enable_CRC ? "Yes" : "No");
		uint8_t modem_config_2 = this->readRegister(REG_MODEM_CONFIG_2);
		uint8_t config = 0;
		if(Is1272())
			config = enable_CRC ? (modem_config_2 | 0x02) : (modem_config_2 & 0xfd);
		else
			config = enable_CRC ? (modem_config_2 | 0x04) : (modem_config_2 & 0xfb);
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
			ASeries.printf("Set implicit header: %s", implicitHeaderMode ? "Yes" : "No");
			this->_ImplicitHeaderMode = implicitHeaderMode;
			uint8_t modem_config_1 = readRegister(REG_MODEM_CONFIG_1);
			uint8_t config = 0;
			if(Is1272())
				config = implicitHeaderMode ? (modem_config_1 | 0x04) : (modem_config_1 & 0xfb);
			else
				config = implicitHeaderMode ? (modem_config_1 | 0x01) : (modem_config_1 & 0xfe);
			writeRegister(REG_MODEM_CONFIG_1, config);
		}
	}

	void Sx127x::PrepIrqHandler(InterruptFn handlefn)
	{
		// attach the handler to the irq pin, disable if None
		if (this->_IrqPin != 0)
		{
			if (handlefn)
			{
				attachInterrupt(digitalPinToInterrupt(this->_IrqPin), handlefn, _ActiveLowIrq ? FALLING : RISING);
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
		_SpiControl->SetSxDir(true);	// enable the RF RX chain
		this->implicitHeaderMode(size > 0);
		if (size > 0)
		{
			this->writeRegister(REG_PAYLOAD_LENGTH, size & 0xff);
		}
		// enable rx to raise DIO0
		if (this->_LoraRcv)
		{
			_IrqFunction = &Sx127x::ReceiveSub;
			this->writeRegister(REG_DIO_MAPPING_1, 0x00);
		}
		else
		{
			_IrqFunction = nullptr;
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
				this->_LastReceivedTime = millis();
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

	// called by the static transmit interrupt handler on TxDone irq
	void Sx127x::TransmitSub(void)
	{
		this->_LastError = "";
		this->acquire_lock(true);			  // lock until flags cleared
		int irqFlags = this->getIrqFlags();
		this->acquire_lock(false);			 // unlock
		if (irqFlags & IRQ_TX_DONE_MASK)
		{
			// it's a transmit finish interrupt
			this->_LastSentTime = millis();
			_IrqFunction = nullptr;		// no one to call right now
			if (this->_LoraRcv)
			{
				this->_LoraRcv->_doTransmit();
				_SpiControl->SetSxDir(true);	// assume receiver section can use a warmup and anyway uses less power but untested
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

	// a static method to receive the interrupt, so this uses _Singleton to call an instance method
	void Sx127x::HandleInterrupt(void)
	{
		if(_Singleton)
			_Singleton->LocalInterrupt();
	}

	// called during interrupt to call the local interrupt function
	void Sx127x::LocalInterrupt()
	{
		if(_IrqFunction)
		{
			(this->*_IrqFunction)();	// TransmitSub or ReceiveSub
		}
		else
		{
			int irqf = getIrqFlags();	// clear whatever caused the interrupt i guess
			if(irqf & 0)
			{
			}
		}
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
		else 
		{
			uint8_t opmode = this->readRegister(REG_OP_MODE);
			if (opmode != (MODE_LONG_RANGE_MODE | MODE_RX_SINGLE) && opmode != (MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS))
			{
				// no packet received and not in receive mode
				// reset FIFO address / // enter continuous RX mode
				this->writeRegister(REG_FIFO_ADDR_PTR, FifoRxBaseAddr);
				this->writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
			}
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
		this->_SpiControl->Transfer(REG_FIFO, tv.Data(), packetLength);	// get all data in one spi call
		// do not use tv[packetLength] here because if the allocate moves the Data then
		// the optimizer uses the wrong pointer...
		tv.Data()[packetLength] = 0;				// null terminate any strings
	}

	uint8_t Sx127x::readRegister(uint8_t address)
	{
		uint8_t response = this->_SpiControl->Transfer(address & 0x7f);
		return response;
	}

	void Sx127x::writeRegister(uint8_t address, uint8_t value)
	{
		this->_SpiControl->Transfer(address | 0x80, value);
	}

	void Sx127x::dumpRegisters() 
	{
		for(int i=0; i<128; i++)
		{
			ASeries.println( String(i) + ":" + String(this->readRegister(i)));
				// "0x{0:02x}: {1:02x}".format(i, this->readRegister(i)));
		}
	}

	// the low data rate flag must be set dependent on the symbol duration > 16ms per spec
	void Sx127x::setLowDataRate()
	{
		// get symbol duration in ms. Spreading factor max=12 so bw/(2**sf) > 6
		uint16_t symbolDuration = 1000 / ( _SignalBandwidth / (1L << _SpreadingFactor) ); 
		if(!Is1272())
		{
		uint8_t config3 = readRegister(REG_MODEM_CONFIG_3); 
			if( symbolDuration > 16)
			{
			config3 |= 8;
			}
			else
			{
			config3 &= ~8;
			}
			//bitWrite(config3, 3, symbolDuration > 16); 	// set the flag on iff >16ms symbol duration
			ASeries.printf("Set low data rate flag register: %d", config3);
			writeRegister(REG_MODEM_CONFIG_3, config3); 
		}
	}

	// This calibrates the system and it reads the current
	// chip temperature as an integer. Standard is around 242 at 25C.
	// The manual says calibration should be done when frequency is set to other than default.
	uint8_t Sx127x::doCalibrate()
	{
		int8_t tempr = 0;
		if(!Is1272())
		{
			uint8_t previousOpMode;

			// save current Operation mode
			uint8_t prevOpMode = readRegister(REG_OP_MODE);
			if(prevOpMode & MODE_LONG_RANGE_MODE)
			{
				// if lora mode, go to lora sleep
				writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
			}

			writeRegister(REG_OP_MODE, MODE_SLEEP);	// put into fsk mode while sleeping
			writeRegister(REG_OP_MODE, MODE_SYNTHESIZER_RX);	// put into fsk rf synth
			uint8_t oldCal = readRegister(REG_IMAGE_CAL);
			writeRegister(REG_IMAGE_CAL, (oldCal & IMAGECAL_TEMPMONITOR_MASK) | IMAGECAL_TEMPMONITOR_ON);	// turn on temp reading

			delay(1);		// wait 1ms

			// disable temp reading
			writeRegister(REG_IMAGE_CAL, (oldCal & IMAGECAL_TEMPMONITOR_MASK) | IMAGECAL_TEMPMONITOR_OFF);	// turn off temp reading
			writeRegister(REG_OP_MODE, MODE_SLEEP);		// put into fsk sleep mode
			tempr = readRegister(REG_TEMP);		// read the temperature

			// as long as we're sleeping and at the right frequency, calibrate...
			writeRegister(REG_OP_MODE, MODE_STDBY);		// put into fsk standby mode for image cal
			writeRegister(REG_IMAGE_CAL, (oldCal & IMAGECAL_IMAGECAL_MASK) | IMAGECAL_IMAGECAL_START);	// start calibration
			int ctr = 0;
			while( IMAGECAL_IMAGECAL_RUNNING & readRegister(REG_IMAGE_CAL))
			{
				delay(1);
				ctr++;
			}
			ASeries.printf("Delayed %dms while calibrating.", ctr);
			writeRegister(REG_OP_MODE, MODE_SLEEP);		// put into fsk sleep mode

			if(prevOpMode & MODE_LONG_RANGE_MODE)
			{
				writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);	// switch back to Lora while sleeping
			}

			writeRegister(REG_OP_MODE, prevOpMode);		// now back to original mode
		}
		return tempr;
	}

