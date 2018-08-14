An Arduino Library for Controlling a Semtech SX127x LoRa Chip
---
This is yet another library for controlling an SX127x Semtech chip. This is entirely interrupt driven (transmit and receive) with callbacks.

For power usage, you can sleep the CPU while waiting for an interrupt during transmit and receive cycles.

There is a nearly exact copy of this library in MicroPython here https://github.com/MZachmann/LightLora_MicroPython

Installation
--
This can be used like a standard Arduino Library and installed into SketchFolder/Libraries as LoraUtil. You can also just embed it in a project. Don't forget Utility.cpp,h

Usage
--
During setup call 
```c++
StringPair parameters[] = {{"enable_CRC", 1}, { StringPair::LastSP, 0}}; // example
LoraUtil* lru = new LoraUtil(pinSS, pinRST, pinINT, parameters);
```
Where parameters may be NULL for defaults or an array of StringPairs terminated by StringPair::LastSP. Any parameters not set in the passed-in group will be set to default (see DEFAULT_PARAMETERS).

During the loop you can
```c++
if(lru->isPacketAvailable())
{
	LoraPacket* pkt = lru->readPacket();
	Serial.println(pkt->msgTxt);
}
...
lru->sendString("Hello World");
```

Customization
---
The ports for the LoRa device are set in the ino file and are arguments for the `LoraUtil` construction.

There is a `BlinkLed` routine in the ino that needs a pin value for the on-board led.

The `SPI` device is run with no choices for pin assignment, as seems to be typical.

The LoraUtil object is a LoraReceiver;  it has callbacks for transmit and receive that can be easily changed.

Cautions
---
Interrupt routines in Arduino are finicky and only support some functions. Set flags and strings and do very little else in the transmit and receive handlers.
