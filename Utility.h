#ifndef ASERIAL_H
#define ASERIAL_H

String GetStrTime();

class ASerial
{
public:
	ASerial(String& caller);
	void Start(int baud);
	size_t println(const String& sprint);
	size_t println(const char* spout);
	size_t print(const char* spout);
	size_t println(int spout, int rng = DEC);
	size_t println(double dout, int rng = 2);
	size_t println(unsigned int spout, int rng = DEC);
	bool available();
	uint8_t read();
	bool InitIfNeeded(bool Force=false);
//	size_t println(const char* spout, int nout = DEC);
private:
	bool _DidInit;
	String _Caller;
	int _Baudrate;
};

extern ASerial ASeries;

#endif
