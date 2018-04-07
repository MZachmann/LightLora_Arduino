#include "Arduino.h"
#include "Utility.h"

// This is just a Serial wrapper. The standard Serial interface should not be used.
// Serial by itself crashes when the port closes and you write to it as well as when
// Writing to it with no serial port. Or it stops dead or...
// So this ensures that the Serial I/O does not cause failures
// It also prepends a header to every println with server name and time
// call the static Serial wrapper "ASeries" so it's easy to search for Serial without a hit

static bool _UseSerial = true;			// set to false to disable serial i/o
static bool _WaitForSerial = false;		// this will halt until Serial is available

static String PROG_NAME("Server");
ASerial ASeries(PROG_NAME);

// get time of day (millis) as hours:minutes.seconds.milliseconds
String GetStrTime()
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

// ---------------------------------------------------------------------
// The Serial wrapper
// ---------------------------------------------------------------------

ASerial::ASerial(String& caller)
{
	_DidInit = false;		// only initialize serial once
	_Caller = caller;		// for logging
	_Baudrate = 19200;	// default baudrate=19200
}

void ASerial::Start(int baud) 
{
	_Baudrate = baud;
	if(_UseSerial)
	{
		InitIfNeeded();
	}
}

// Serial mimic methods
size_t ASerial::println(const char* spout)
{
	if(_UseSerial && InitIfNeeded())
	{
		return Serial.println(_Caller + "@" + GetStrTime() + spout);
	}
	return 0;
}

size_t ASerial::println(const String& sprint)
{
	return println(sprint.c_str());
}

size_t ASerial::print(const char* spout)
{
	if(_UseSerial && InitIfNeeded())
	{
		return Serial.print(GetStrTime() + String(spout));
	}
	return 0;
}

size_t ASerial::println(int nout, int rng)
{
	String snout(nout, rng);
	return println(snout);
}

size_t ASerial::println(unsigned int uout, int rng)
{
	String suout(uout, rng);
	return println(suout);
}

size_t ASerial::println(double dout, int rng)
{
	String sdout(dout, rng);
	return println(sdout);
}

bool ASerial::InitIfNeeded(bool Force)
{
	if(Force || !Serial)
	{
		_DidInit = false; // lost signal ? 
	}

	if(_DidInit)
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

bool ASerial::available()
{
	return Serial && Serial.available();
}

uint8_t ASerial::read()
{
	return Serial ? Serial.read() : 0;
}
   
