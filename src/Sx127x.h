#ifndef SX_127X
#define SX_127X
// Companion Header file to sx127x.cpp
// Requires: spicontrol.cpp, h; tinyvector.cpp, h, 
//   and ../../Utility.cpp,h (for Serial i/o)
// This is an Arduino controller for the Semtech sx1276 and cousins
// It is fully interrupt driven (transmit and receive)


class TinyVector;

class SpiControl;

// these are here so we can default to boost pin
#define PA_OUTPUT_RFO_PIN 0
#define PA_OUTPUT_PA_BOOST_PIN 1

// we pass in the address of our LoraReceiver to get interrupt driven stuff
// these methods should be very fast and can't do things like delay or Serial.print
class LoraReceiver
{
	public:
		virtual void _doReceive(TinyVector* payload) = 0;
		virtual void _doTransmit() = 0;
};

// to create a Python dictionary-like object
class StringPair
{
	public:
		StringPair(const char* name, int32_t value);
		static const char* LastSP;	// Last for string list
		const char* Name;
		const int32_t Value;
};

// the method prototype for what we pass to the interrupt handler
typedef void (*InterruptFn)(void);

class Sx127x;
typedef void (Sx127x::*LocalInterruptFn)(void);

class Sx127x
{
	public:
		Sx127x();		// empty constructor
		void Initialize(String* name=NULL, SpiControl* spic=NULL);
		virtual ~Sx127x();
		
	// parameters is a list of StringPairs ending with { StringPair::LastSP, 0}
	// they are option name, value. Defaults are:
	// {"frequency", 915}, 			# frequency in MHz
	// {"frequency_low", 0},		# remaining frequency in Hz
	// {"tx_power_level", 2},   	# power level. 0...14 or 2...17
	// {"signal_bandwidth", 125000},    # signal bandwidth in Hz
	// {"spreading_factor", 7},		# width of signal
	// { "coding_rate", 5}, 		# 4 / 5...8 as crc
	// {"preamble_length", 8}, {"implicitHeader", 0}, {"sync_word", 0x12}, {"enable_CRC", 0},
	// {"power_pin", PA_OUTPUT_PA_BOOST_PIN}
		bool init(const StringPair* parameters =NULL);			// must be called first. Returns false if not detected
		void setReceiver(LoraReceiver* receiver);			// use a receiver class on interrupts
		const String& lastError();							// get the last error message if there was one during interrupt
		void clearLastError();								// clear the prior error message

		void beginPacket(bool implicitHeaderMode=false);	// call before sending a packet
		void endPacket(); 									// call after filling the fifo to send the packet
		bool isTxDone(); 									// synchronous is transmit complete. clears flag when called.
		int writeFifo(const uint8_t* buffer, int size);		// write bytes to the fifo
		void acquire_lock(bool lock=false);					// lock and unlock
		uint8_t getIrqFlags(); 								// read the irq flags and clear them by writing them
		uint32_t getLastReceivedTime(void);					// when last got an interrupt
		uint32_t getLastSentTime(void);						// when last got an interrupt
		int packetRssi(); 									// get last packet rssi
		float packetSnr(); 									// get last packet Signal to noise ratio
		void standby(); 									// put chip in standby
		void sleep(); 										// put chip to sleep
		uint8_t doCalibrate();								// run calibration
		void setTxPower(int level, int outputPin=PA_OUTPUT_PA_BOOST_PIN);	// set the power level
		void setFrequency(double frequency);				// set the center frequency (in Hz)
		void setFrequencyOffset(double frequency);			// set the frequency deviation
		void setSpreadingFactor(int sf);					// set spread factor exponent (2**x)
		void setSignalBandwidth(int sbw);					// set the signal bandwidth
		void setCodingRate(int denominator);				// set coding rate denominator (num=4). 4,5,7,8
		void setPreambleLength(int length);					// set preamble length
		void enableCRC(bool enable_CRC=false);				// enable the crc?
		void setSyncWord(int sw);
		void dumpRegisters(); 								// write all the registers to Serial
		void implicitHeaderMode(bool implicitHeaderMode=false);	// set the implicit header mode
		void receive(int size=0);							// prepare to receive
		bool receivedPacket(int size=0);					// is there a received packet (synchronous)
		void ReadPayload(TinyVector& tv);					// read the payload from the rcvd packet
		uint8_t readRegister(uint8_t address);				// read an sx127x register
		void writeRegister(uint8_t address, uint8_t value);	// write to an sx127x register
		void setLowDataRate();								// set the low data rate flag based on symbol duration
	private:
		// these all deals with interrupts
		void PrepIrqHandler(InterruptFn handlefn);		// set the hardware interrupt handler
		static void HandleInterrupt();		// which points to this always
		void LocalInterrupt();				// which calls this always...
		void ReceiveSub();					// is called on receive packet
		void TransmitSub();					// is called on packet sent

		// properties
		bool _Lock;
		String _Name;
		String _LastError;
		int _IrqPin;				// the irq pin
		bool _ImplicitHeaderMode;
		uint8_t	_SpreadingFactor;	// the spreading factor setting
		uint32_t _SignalBandwidth;	// the signal bandwidth
		double _Frequency;			// in Hz
		double _FrequencyOffset;	// for temperature and static compensation
		uint32_t _LastReceivedTime;	// last receive interrupt time in milliseconds
		uint32_t _LastSentTime;		// last send interrupt time in milliseconds
		LoraReceiver* _LoraRcv;		// who we call on interrupt
		SpiControl* _SpiControl;	// the SPI wrapper
		TinyVector* _FifoBuf;		// a semi-persistant buffer
		LocalInterruptFn _IrqFunction; // who to call on interrupt
};

#endif
