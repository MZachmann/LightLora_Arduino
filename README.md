An Arduino Library for Controlling a Semtech SX127x LoRa Chip
---
This is yet another library for controlling an SX127x Semtech chip. This is entirely interrupt driven (transmit and receive) with callbacks.

For power usage, you can sleep the CPU while waiting for an interrupt during transmit and receive cycles.

There is a nearly exact copy of this library in MicroPython here ??

Installation
--
This can be used like a standard Arduino Library and installed into SketchFolder/Libraries as LoraUtil. You can also just embed it in a project. Don't forget Utility.cpp,h

Usage
--
During setup call 
```c++
LoraUtil* lru = LoraUtil::LoraUtil(pinSS, pinRST, pinINT)
```
During the loop you can
```c++
while(!lru->isPacketAvailable())
	;
LoraPacket pkt = lru->readPacket();
Serial.println(pkt->msgTxt);
...
String txt = "Hello World";
lru->sendString(txt);
```

Customization
---
The ports for the LoRa device are set in the ino file and are arguments for the `LoraUtil` construction.

There is a `BlinkLed` routine in the ino that needs a pin value for the on-board led.

The `SPI` device is run with no choices for pin assignment, as seems to be typical.

Cautions
---
Interrupt routines in Arduino are finicky and only support some functions. Set flags and strings and do very little else in the transmit and receive handlers.
