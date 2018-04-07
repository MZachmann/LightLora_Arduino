#ifndef SERIALWRAP_H
#define SERIALWRAP_H

// a simple wrapper for Serial that enables:
// - more robust when no Serial or disconnect
// - actively reconnects when possible
// - can log to a string instead of Serial
// - prepends time (optional) to the output string

// user-provided time formatter
typedef String (*SerialTimeFormatter)();

class SerialWrap
{
public:
	SerialWrap(String& caller);
	void Start(int baud);
	bool InitIfNeeded(bool Force=false);	// (re)initialize the port
	void SetFormatter(SerialTimeFormatter timeFormatter);	// override the default formatter
	static String getStrTime(void);			// the default time formatter
	// Serial mimic stuff
	size_t println(const String& sprint);
	size_t println(const char* spout);
	size_t print(const char* spout);
	size_t println(int spout, int rng = DEC);
	size_t println(double dout, int rng = 2);
	size_t println(unsigned int spout, int rng = DEC);
	size_t printf(const char * format, ...);
	
	bool available();
	uint8_t read();
	// for when serial isn't going to work...
	bool isLogging();
	String setLogging(bool doLog);	// returns the log when doLog==false

private:
	bool _IsLogging;
	String _SerialLog;
	bool _DidInit;
	String _Caller;
	int _Baudrate;
	SerialTimeFormatter _TimeFormatter;
};

extern SerialWrap ASeries;

#endif
