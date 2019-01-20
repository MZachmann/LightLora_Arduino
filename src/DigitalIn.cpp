#include "Arduino.h"
#include "DigitalPin.h"
#include "DigitalIn.h"

// Simple digital input facade. Can be constructed immediately or delayed

    DigitalIn::DigitalIn() : _Pin(NOPIN)
    {
    }

    // Create a DigitalIn connected to the specified pin
	DigitalIn::DigitalIn(uint8_t pin)
    {
		SetPin(pin);
    }

    // Create a DigitalIn connected to the specified pin
    DigitalIn::DigitalIn(uint8_t pin, uint8_t mode)
    {
		SetPin(pin, mode);
    }

    void DigitalIn::SetPin(uint8_t pin) 
    {
		SetPin(pin, INPUT);
    }

    void DigitalIn::SetPin(uint8_t pin, uint8_t mode) 
    {
		_Pin = pin;
		pinMode(pin, mode);
    }

	uint8_t DigitalIn::GetPin(void)
	{
		return _Pin;
	}

	bool DigitalIn::IsInitialized(void)
	{
		return _Pin != NOPIN;
	}

    // Read the pin, return 0, 1
    int DigitalIn::Read()
    {
        if(_Pin == NOPIN)
        {
            // assert?
            return 0;
        }
        return digitalRead(_Pin) ? 1 : 0;
    }

    // Set the input pin mode nrf_gpio_pin_pull_t
    void DigitalIn::Mode(uint8_t pull)
    {
        if(_Pin != NOPIN)
			pinMode(_Pin, pull);
    }

    // A shorthand for read()
    DigitalIn::operator int()
    {
        // Underlying call is thread safe
        return (HIGH == Read()) ? 1 : 0;
    }
