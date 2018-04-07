#ifndef TINY_VECTOR
#define TINY_VECTOR

// a vaguely efficient resizable uint8_t array

class TinyVector
{
	public:
		// construction
		TinyVector(uint16_t initialSize = 0, uint16_t extraSize = 0);
		virtual ~TinyVector();
		bool Allocate(uint16_t size, uint16_t extra = 0);
		// access
		uint16_t Size();
		uint8_t* Data();	// using the usual operator[] override failed badly when allocate changed _Data
		// diagnostic
		String ToString();

	private :
		uint16_t _MaxLength;		// how many bytes are allocated
		uint16_t _Length;			// external-facing size
		uint8_t* _Data;	// data buffer
};

#endif
