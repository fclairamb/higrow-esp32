# Higrow / ESP32

## The board and project
The project and the board have been started by [lucafabri](https://github.com/lucafabbri/HiGrow-Arduino-Esp) who seems to have moved to other things since then.

It can be bought on:
- [amazon](https://www.amazon.com/dp/B07J9LRJ4T/): This is the board I used
- [aliexpress](https://www.aliexpress.com/i/32969456777.html)

## What this program does
- Connect to one of the known wifi networks
- Read the soil moisture sensor
- Send the sensor value on the eedomus home automation cloud. This part be easily replaced by an other home auomation solution.

## Known issues
- Light reading doesn't work, it seems to be an hardware issue
- Coming back from deep sleep is not properly handled, probably a software issue. It has no impact at this stage thought
- The current consumption never goes below 40mA, thus making it unsuited for low consumption
- The program might get stuck if there's a (even temporary) wifi connection issue. As such there's a timer-based software watchdog logic to restart the devicee when this happens.

## Getting started with it
- Install the [USB to serial driver](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)
- Install the [Arduino IDE](https://www.arduino.cc/en/Main/Software)
- Setup the [ESP32 board](https://github.com/espressif/arduino-esp32/blob/master/docs/arduino-ide/boards_manager.md)
- Install the [DHT sensor library for ESPx](https://www.arduinolibraries.info/libraries/dht-sensor-library-for-es-px)
- In the Aruino IDE, in tools,
  - Select the matching serial port in the IDE (`/dev/tty.SLAB_USBtoUART` on Mac Os X)
  - Select the board type "ESP32 Dev Module", the programmer "AVRISP mkII"
  - Load the folder of this repository
  - Build & Start the upload (CMD+U)
  - Press the "boot" button for a few seconds
