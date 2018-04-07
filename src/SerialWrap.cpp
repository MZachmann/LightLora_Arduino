#include "Arduino.h"
#include "SerialWrap.h"

// This is just a Serial wrapper. The standard Serial interface should not be used.
// Serial by itself crashes when the port closes and you write to it as well as when
// Writing to it with no serial port. Or it stops dead or...
// So this ensures that the Serial I/O does not cause failures
// It also prepends a header to every println with server name and time
// call the static Serial wrapper "ASeries" so it's easy to search for Serial without a hit

static bool _UseSerial = true;			// set to false to disable serial i/o
static bool _WaitForSerial = false;		// this will halt until Serial is available

static String PROG_NAME("Server");
SerialWrap ASeries(PROG_NAME);

// ---------------------------------------------------------------------
// The Serial wrapper
// ---------------------------------------------------------------------

SerialWrap::SerialWrap(String& caller)
{
	_IsLogging = false;
	_DidInit = false;		// only initialize serial once
	_Caller = caller;		// for logging
	_Baudrate = 19200;		// default baudrate=19200
	SetFormatter(NULL);		// default time formatter
}

// let this have an app-custom formatter so that the app can use an RTC for time instead of millis()
// which is required to display time accurately given standby w/o clock
void SerialWrap::SetFormatter(SerialTimeFormatter timeFormatter)
{
	if(timeFormatter != NULL)
	{
		_TimeFormatter = timeFormatter;				// use app-provided
	}
	else
	{
		_TimeFormatter = SerialWrap::getStrTime;	// use millis
	}
}

void SerialWrap::Start(int baud) 
{
	_Baudrate = baud;
	if(_UseSerial)
	{
		InitIfNeeded();
	}
}

bool SerialWrap::isLogging()
{
	return _IsLogging;
}

// for places where we want to keep sending to serial i/o but there is none.
// turn on logging to put stuff into the _SerialLog object
// it returns the current Log if doLog is false
// it clears the current log (so ignore result) if doLog is true
String SerialWrap::setLogging(bool doLog)
{
	if(doLog)
	{
		// let us call this with true to get the log repeatedly
		if(!_IsLogging)
		{
			_IsLogging = true;
			_SerialLog = "";
		}
	}
	else
	{
		_IsLogging = false;
		InitIfNeeded();
	}
	return _SerialLog;
}

// Serial mimic methods
size_t SerialWrap::println(const char* spout)
{
	if(_IsLogging)
	{
		_SerialLog += "**" + _Caller + "@" + (*SerialWrap::_TimeFormatter)() + spout + "\r\n";
	}
	else if(_UseSerial && InitIfNeeded())
	{
		return Serial.println(_Caller + "@" + (*SerialWrap::_TimeFormatter)() + spout);
	}
	return 0;
}

size_t SerialWrap::println(const String& sprint)
{
	return println(sprint.c_str());
}

size_t SerialWrap::print(const char* spout)
{
	if(_IsLogging)
	{
		_SerialLog += (*SerialWrap::_TimeFormatter)() + String(spout);
	}
	else if(_UseSerial && InitIfNeeded())
	{
		return Serial.print((*SerialWrap::_TimeFormatter)() + String(spout));
	}
	return 0;
}

size_t SerialWrap::println(int nout, int rng)
{
	String snout(nout, rng);
	return println(snout);
}

size_t SerialWrap::println(unsigned int uout, int rng)
{
	String suout(uout, rng);
	return println(suout);
}

size_t SerialWrap::println(double dout, int rng)
{
	String sdout(dout, rng);
	return println(sdout);
}

bool SerialWrap::InitIfNeeded(bool Force)
{
	if(Force || !Serial)
	{
		_DidInit = false; // lost signal ? 
	}

	if(_DidInit || _IsLogging)
	{
		return true;
	}

	int onesec = 10;    // 10 * 10ms
	// wait up to 100ms serial to become valid
	while (onesec > 0 && !Serial)
	{
		delay(10);   // 100ms wait
		onesec--;
	}
	// we've given it up to a second to start up
	// waitforserial waits forever
	while (_WaitForSerial && !Serial)
	{
		delay(100);   // 100ms wait
	}
	// if we have one start it at 19200
	if(Serial)
	{
		Serial.begin(_Baudrate);
		_DidInit = true;
	}
	delay(10);
	return this->available();
}

bool SerialWrap::available()
{
	return (Serial && Serial.available());
}

uint8_t SerialWrap::read()
{
	return (Serial ? Serial.read() : 0);
}
   
// get time of day (millis) as hours:minutes.seconds.milliseconds
String SerialWrap::getStrTime()
{
	// format into hours, minutes seconds
	long nows = millis(); // time in millis
	int onehour = 1000 * 60 * 60;
	int onemin = 1000 * 60;
	int hours = nows / onehour;
	int minutes = (nows - hours * onehour) / onemin;
	int mills = (nows - hours * onehour - minutes * onemin);
	int seconds = mills / 1000;
	mills = mills - seconds * 1000;
	return String(hours) + ":" + String(minutes) + "." + String(seconds) + "." + String(mills) + "  ";
}

