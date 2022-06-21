# Wireless DS18B20 Temperature Sensor

[![Arduino Compile Sketches](https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor/actions/workflows/arduino-compile-sketches.yml/badge.svg)](https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor/actions/workflows/arduino-compile-sketches.yml)
[![Check Markdown Links](https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor/actions/workflows/CheckMarkdownLinks.yml/badge.svg)](https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor/actions/workflows/CheckMarkdownLinks.yml)

The wireless temperature sensor is designed to use an ESP8266 controller and two DS18B20 temperature sensors using the 1-Wire protocol. The specific devices used in my project are SparkFun's [ESP8266 Thing - Dev Board][1] and SparkFun's [waterproof DS18B20][2] sensors, but the code should work as-is or with minor modifications on other ESP8266 and DS18B20 devices.

The sensor sends data to [ThingSpeak's IoT][3] platform using MQTT. This requires the definition of various user-specific values and keys which are defined in header files not included in this repo. Instructions for the format of these header files are included in the sketch comments.

## Hardware

This particular sensor module is line powered, and is therefore not optimized for low-power operation. The DS18B20 temperature sensors are connected in external power mode (i.e., not using parasitic power), and the ESP8266 is always fully on and does not use any low-power modes.

The signal pin definition is near the top of the sketch and can be changed as needed.

```cpp
DS18B20_SIGNAL_PIN  4    // DQ pin, with a 4.7 K Ohm pullup to Vcc
```

Since this is designed for ESP8266, remember to ground D0 during reset to put it the processor into programming mode (my hardware interface board has header pins to make this easier).

I have two sensors connected (both to the same OneWire signal pin), one with about 25 ft of cabling. The setup works reliably with the standard 4.7 KOhm pullup resistor.

## External Libraries

Adafruit [MQTT Library][5]

- This sketch has been tested with library version 0.20.1 with the following modification:
  - In library file Adafruit_MQTT.cpp, comment out lines 425-431 to remove support for floating point. Specifically, comment out the block starting with:

    ``` cpp
    else if (sub->callback_double != NULL)
    ```

- Newer library versions may also work, or may require some minor code changes.

## References

- [DS18B20][4] Digital Thermometer
- SparkFun [ESP8266 Thing - Dev][1]
- SparkFun [Waterproof Temperature Sensor with DS18B20][2]
- Adafruit [MQTT Library][5]

## License

The software and other files in this repository are released under what is commonly called the [MIT License][100]. See the file [`LICENSE`][101] in this repository.

[1]: https://www.sparkfun.com/products/13711
[2]: https://www.sparkfun.com/products/11050
[3]: https://thingspeak.com/
[4]: https://cdn.sparkfun.com/datasheets/Sensors/Temp/DS18B20.pdf
[5]: https://github.com/adafruit/Adafruit_MQTT_Library
[100]: https://choosealicense.com/licenses/mit/
[101]: ./LICENSE
[200]: https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor
