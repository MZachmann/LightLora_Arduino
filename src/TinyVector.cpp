// --------------------------------------------------------------------
// TinyVector is a C++ equivalent to Python bytearray
// It has a size and buffer and can be resized
// When resized downward it leaves the allocation for performance reasons. 
// So it's good as a static buffer and acceptable for passing around
// MZachmann - March 13, 2018
// --------------------------------------------------------------------
#include "Arduino.h"
#include "TinyVector.h"


// This is a simplistic vector with just enough oomph for the sx127x controller
TinyVector::TinyVector(uint16_t initialSize, uint16_t extraSize) 
{
	 _Length = 0;
	 _MaxLength = 0;
	 _Data = NULL;
	if(initialSize > 0 || extraSize > 0)
	{
		Allocate(initialSize, extraSize);
	}
}

TinyVector::~TinyVector()
{
	// avoid leaking
	 if(_Data)
	 {
 		delete _Data; 
	 }
}

// note this doesn't zero anything, it does copy on a grow
// in std::vector this is resize for a vector
bool TinyVector::Allocate(uint16_t size, uint16_t extra)
{
	if(_MaxLength == 0)
	{
		// first allocation.
		_Length = size;
		_MaxLength = size + extra;
		_Data = (uint8_t*)malloc(_MaxLength);
	}
	else if(_MaxLength > (size+extra))
	{
		// just change nominal size if shrinking
		_Length = size;
	}
	else
	{
		// we're growing. make a new buffer and copy in the old one
		uint8_t* newd = (uint8_t*)malloc(size + extra);
		int ml = (_Length > size) ? size : _Length;		// min
		if(ml > 0)
		{
			memcpy(newd, _Data, ml);
		}
		_Length = size;
		_MaxLength = size + extra;
		if(_Data)	// check should be not needed, but
		{
			delete _Data;
		}
		_Data = newd;
	}

	return (_Data != NULL);
}

static uint8_t zero = 0;

// length of vector, not available space
uint16_t TinyVector::Size() 
{
	 return _Length; 
}

// for referring to the buffer directly
uint8_t* TinyVector::Data() 
{
	 return _Data; 
}

// dump to string for diagnostics
String TinyVector::ToString()
{
	String x = "";
	int i;
	for(i=0; i<_Length; i++)
	{
		x = x + "." + String(_Data[i]);
	}
	for(; i<_MaxLength; i++)
	{
		x = x + "+" + String(_Data[i]);
	}
	return x;
}

