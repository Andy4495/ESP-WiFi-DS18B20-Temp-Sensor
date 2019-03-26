/**
   Sensor Receiver Station
   https://gitlab.com/Andy4495/Pond-Sensor-With-WiFi

   1.0 - 03/24/2019 - A.T. - Original.
*/

/**
   Temperature sensor for Pond.
   - Implemented on ESP8266 using SparkFun ESP8266 ThingDev
   - Uses DS18B20 submergible temperature sensor
   - Connectes to ThingSpeak IoT platform using WiFi

   Receiver hub for various sensor transmitting modules.
   Uses Anaren CC110L BoosterPack as receiver module.
   ** Note that the program will freeze in setup() if
   ** the CC110L BoosterPack is not installed.

**/
/**
   EXTERNAL LIBRARIES:
   - MQTT: https://github.com/adafruit/Adafruit_MQTT_Library
       - Adafruit_MQTT.cpp modified to comment out lines 425-431
         to remove support for floating point. Specifically,
         commented out the block starting with:
            "else if (sub->callback_double != NULL)"
*/

/* BOARD CONFIGURATION AND OTHER DEFINES
   -------------------------------------

   To reduce output sent to Serial, keep the following line commented:
     //#define PRINT_ALL_CLIENT_STATUS
   It can be uncommented to help debug connection issues

   To reduce RAM (and program space) usage, disable Serial prints by
   commenting the line:
     //#define SKETCH_DEBUG
*/
//#define PRINT_ALL_CLIENT_STATUS
#define SKETCH_DEBUG

#define NETWORK_ENABLED

#ifdef SKETCH_DEBUG
#define SKETCH_PRINT(...) { Serial.print(__VA_ARGS__); }
#define SKETCH_PRINTLN(...) { Serial.println(__VA_ARGS__); }
#else
#define SKETCH_PRINT(...) {}
#define SKETCH_PRINTLN(...) {}
#endif

#define BOARD_LED 5       // On ThingDev, LED is connected to Pin 5
#define DS18B20_SIGNAL_PIN  4

#include <ESP8266WiFi.h>
//#include "adc_mode.h"     // Needed in order to use getVcc()
#include <OneWire.h>

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "MQTT_private_config.h"
/* The MQTT_private_config.h file needs to include the following definitions
   specific to your configuration:
     byte mac[] = {6 byte MAC address for ethernet card};
     #define AIO_SERVER      "address of your MQTT server (e.g. io.adafruit.com)"
     #define AIO_SERVERPORT  Port number of your MQTT server, e.g. 1883
     #define AIO_USERNAME    "Username for MQTT server account"
     #define AIO_KEY         "MQTT key required for your MQTT server account"
   If using ThingSpeak, then WRITE keys for each channel may also be #defined here.
*/
WiFiClient client_ts;
Adafruit_MQTT_Client thingspeak(&client_ts, TS_SERVER, TS_SERVERPORT, TS_USERNAME, TS_KEY);
#define PAYLOADSIZE 132
char payload[PAYLOADSIZE];    // MQTT payload string
#define FIELDBUFFERSIZE 20
char fieldBuffer[FIELDBUFFERSIZE];  // Temporary buffer to construct a single field of payload string

struct PondData {
  int             MSP_T;          // Tenth degrees F
  int             Submerged_T;    // Tenth degrees F
  unsigned int    Batt_mV;        // milliVolts
  int             Pump_Status;    // Unimplemented --> Redefined to indicate WiFi RSSI
  int             Aerator_Status; // Unimplemented
  unsigned long   Millis;         // --> Redefined to indicate number of times through loop()
  int             Battery_T;      // Tenth degrees F
};

// -----------------------------------------------------------------------------
/**
    Global data
*/

PondData ponddata;
int lostConnectionCount = 0;
unsigned long numberOfLoops = 0;

OneWire  ds18b20(DS18B20_SIGNAL_PIN);  // Requires a 4.7K pull-up resistor
// Address is not needed since we only have one device on the bus
uint8_t scratchpad[9];
// OneWire commands
#define GETTEMP         0x44  // Tells device to take a temperature reading and put it on the scratchpad
#define COPYSCRATCH     0x48  // Copy EEPROM
#define READSCRATCH     0xBE  // Read EEPROM
#define WRITESCRATCH    0x4E  // Write to EEPROM
// Scratchpad locations
#define TEMP_LSB        0
#define TEMP_MSB        1
#define HIGH_ALARM_TEMP 2
#define LOW_ALARM_TEMP  3
#define CONFIGURATION   4
#define INTERNAL_BYTE   5
#define COUNT_REMAIN    6
#define COUNT_PER_C     7
#define SCRATCHPAD_CRC  8
// Device resolution
#define TEMP_9_BIT  0x1F //  9 bit
#define TEMP_10_BIT 0x3F // 10 bit
#define TEMP_11_BIT 0x5F // 11 bit
#define TEMP_12_BIT 0x7F // 12 bit

/***** MQTT publishing feeds *****
   Each feed/channel that you wish to publish needs to be defined.
     - Adafruit IO feeds follow the form: <username>/feeds/<feedname>, for example:
         Adafruit_MQTT_Publish pressure = Adafruit_MQTT_Publish(&mqtt,  AIO_USERNAME "/feeds/pressure");
     - ThingSpeak Channels follow the form: channels/<CHANNEL_ID>/publish/<WRITE_API_KEY>, for example:
         Adafruit_MQTT_Publish myChannel = Adafruit_MQTT_Publish(&mqtt,
                                        "channels/" CHANNEL_ID "/publish/" CHANNEL_WRITE_API_KEY);
         See https://www.mathworks.com/help/thingspeak/publishtoachannelfeed.html
     - Cayenne Channel format:
     Adafruit_MQTT_Publish Topic = Adafruit_MQTT_Publish(&cayenne,
                                "v1/" CAY_USERNAME "/things/" CAY_CLIENT_ID "/data/" CAY_CHANNEL_ID);
       See https://mydevices.com/cayenne/docs/cayenne-mqtt-api/#cayenne-mqtt-api-mqtt-messaging-topics
   The file "MQTT_private_feeds.h" needs to include the feed/channel definitions
   specific to your configuration.
*/
#include "MQTT_private_feeds.h"

void setResolution(uint8_t resolution) {
  ds18b20.reset();
  ds18b20.skip();                          // Only one device on the bus, so don't need to bother with the address
  ds18b20.write(WRITESCRATCH);             // no parasitic power (2nd argument defaults to zero)

  scratchpad[CONFIGURATION] = resolution;  // Set the resolution value. Don't care about TH and TL, so don't bother setting.

  for (int i = HIGH_ALARM_TEMP; i <= CONFIGURATION; i++) {  // 3 bytes required for the WRITESCRATCH command
    ds18b20.write(scratchpad[i]);
  }

  ds18b20.reset();
  ds18b20.skip();                          // Only one device on the bus, so don't need to bother with the address
  ds18b20.write(COPYSCRATCH);              // no parasitic power (2nd argument defaults to zero)

  delay(15);                               // Need a minimum of 10ms per datasheet after copy scratch
}

void setup()
{
  pinMode(BOARD_LED, OUTPUT);       // Note that ESP Thing LED has reverse polarity

  // Setup serial for status printing.
#ifdef SKETCH_DEBUG
  Serial.begin(9600);
#endif

  SKETCH_PRINTLN(F(" "));
  SKETCH_PRINTLN(F("ESP8266 pond sensor with WiFi."));
  delay(500);

  SKETCH_PRINTLN(F("Starting WiFi..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while ( WiFi.status() != WL_CONNECTED) {
    // print dots while we wait to connect
    SKETCH_PRINT(".");
    delay(300);
  }

  SKETCH_PRINTLN("\nYou're connected to the network");
  SKETCH_PRINTLN("Waiting for an ip address");

  while (WiFi.localIP() == INADDR_NONE) {
    // print dots while we wait for an ip addresss
    SKETCH_PRINT(".");
    delay(300);
  }

  SKETCH_PRINTLN(F("WiFi enabled."));
  digitalWrite(BOARD_LED, LOW);

#ifdef SKETCH_DEBUG
  printWifiStatus();
#endif

  SKETCH_PRINTLN("Attempting to connect to Thingspeak...");
  MQTT_connect(&thingspeak, &client_ts);

  digitalWrite(BOARD_LED, HIGH);
  delay(500);
  digitalWrite(BOARD_LED, LOW);

  // Read scratchpad to get current resolution value
  ds18b20.reset();
  ds18b20.skip();                    // Only one device on the bus, so don't need to bother with the address
  ds18b20.write(READSCRATCH);        // no parasitic power (2nd argument defaults to zero)

  for (int i = 0; i < 9; i++) {      // Read all 9 bytes
    scratchpad[i] = ds18b20.read();
  }

  switch (scratchpad[CONFIGURATION]) {
    case TEMP_9_BIT:
      setResolution(TEMP_10_BIT);         // Change resolution to 10 bits
      break;
    case TEMP_10_BIT:
      break;
    case TEMP_11_BIT:
      setResolution(TEMP_10_BIT);         // Change resolution to 10 bits
      break;
    case TEMP_12_BIT:
      setResolution(TEMP_10_BIT);         // Change resolution to 10 bits
      break;
    default:
      // Note: Unexpected CONFIG value
      break;
  }
}

void loop()
{
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).
  //  MQTT_connect(&cayenne, &client_cayenne);
  MQTT_connect(&thingspeak, &client_ts);

  process_ponddata();

  SKETCH_PRINTLN(F("--"));
  digitalWrite(BOARD_LED, HIGH);
  delay(500);
  digitalWrite(BOARD_LED, LOW);

  // ping the server to keep the mqtt connection alive
  if (! thingspeak.ping()) {
    thingspeak.disconnect();
    SKETCH_PRINTLN(F("ThingSpeak MQTT ping failed, disconnecting."));

  }

  numberOfLoops++;
  delay(3UL * 60UL * 1000UL);
}


void process_ponddata() {
  int16_t celsius, fahrenheit;

  // Read the temperature from the DS18B20
  // Start temperature conversion
  ds18b20.reset();
  ds18b20.skip();                // Only one device on the bus, so don't need to bother with the address
  ds18b20.write(GETTEMP);        // no parasitic power (2nd argument defaults to zero)
  delay(225);     // 10-bit needs 187.5 ms for conversion, add a little extra just in case

  // Read back the temperature
  ds18b20.reset();
  ds18b20.skip();
  ds18b20.write(READSCRATCH);
  for ( int i = 0; i < 2; i++) {           // Only need 2 bytes to get the temperature
    scratchpad[i] = ds18b20.read();
  }

  // Convert the data to actual temperature
  int16_t raw = (scratchpad[1] << 8) | scratchpad[0]; // Put the temp bytes into a 16-bit integer
  raw = raw & ~0x03;               // 10-bit resolution, so ignore 2 lsb
  // Raw result is in 16ths of a degree Celsius
  // fahrenheit = celsius * 1.8 + 32.0, but we want to use integer math
  celsius = (raw * 10) >> 4;                   // Convert to 10th degree celsius
  fahrenheit = ((celsius * 9) / 5) + 320;      // C to F using integer math (values are in tenth degrees)
  ponddata.Submerged_T = fahrenheit;

  SKETCH_PRINT("WiFi RSSI: ");
  SKETCH_PRINTLN(WiFi.RSSI());

  SKETCH_PRINTLN(F("Sending pond sensor info. "));
  SKETCH_PRINT(F("Submerged Temperature (F): "));
  SKETCH_PRINT(ponddata.Submerged_T / 10);
  SKETCH_PRINT(F("."));
  SKETCH_PRINTLN(ponddata.Submerged_T % 10);
  ponddata.Batt_mV = 3300;
  //  SKETCH_PRINT("ESP8266 Vcc: ");
  //  SKETCH_PRINTLN(ESP.getVcc());
  ponddata.Pump_Status = (int) WiFi.RSSI();
  ponddata.Aerator_Status = 0;
  ponddata.Millis = numberOfLoops;
  ponddata.Battery_T = 0;

  payload[0] = '\0';
  BuildPayload(payload, fieldBuffer, 1, ponddata.MSP_T);
  BuildPayload(payload, fieldBuffer, 2, ponddata.Submerged_T);
  BuildPayload(payload, fieldBuffer, 3, ponddata.Batt_mV);
  BuildPayload(payload, fieldBuffer, 4, ponddata.Millis);
  BuildPayload(payload, fieldBuffer, 5, ponddata.Pump_Status);
  BuildPayload(payload, fieldBuffer, 6, ponddata.Aerator_Status);
  BuildPayload(payload, fieldBuffer, 7, ponddata.Battery_T);
  SKETCH_PRINTLN(F("Sending data to ThingSpeak..."));
  SKETCH_PRINT(F("Payload: "));
  SKETCH_PRINTLN(payload);
  if (! Pond_Sensor.publish(payload)) {
    SKETCH_PRINTLN(F("Pond_Sensor Channel Failed to ThingSpeak."));
  }

} // process_ponddata()


// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void MQTT_connect(Adafruit_MQTT_Client * mqtt_server, WiFiClient * client ) {
  int8_t ret;

  // Return if already connected.
  if (mqtt_server->connected()) {
    return;
  }

  SKETCH_PRINTLN(F("MQTT Disconnected."));

  SKETCH_PRINT(F("Attempting reconnect to MQTT: "));
  SKETCH_PRINTLN(millis());
  ret = mqtt_server->connect();

}

/*******************************************************************
  // BuildPayload() functions for ThingSpeak MQTT
  // See https://www.mathworks.com/help/thingspeak/publishtoachannelfeed.html
  // Overloaded function formats data field based on parameter type.
  // Be sure to set msgBuffer[0] = '\0' to start a new payload string
  // Use fieldNum==12 to format the Status field
*******************************************************************/

// This is the "worker" version that is called by all other versions of the function
// It is also used if a string is already available and does not need to be converted
void BuildPayload(char* msgBuffer, int fieldNum, char* dataField) {
  char  numBuffer[4];
  numBuffer[0] = '\0';

  if (fieldNum < 9) {
    if (msgBuffer[0] == '\0')
      strcat(msgBuffer, "field");
    else
      strcat(msgBuffer, "&field");
    snprintf(numBuffer, 4, "%d", fieldNum);
    strcat(msgBuffer, numBuffer);
    strcat(msgBuffer, "=");
    strcat(msgBuffer, dataField);
  }
  else { // fieldNum >= 9
    if (msgBuffer[0] == '\0')
      strcat(msgBuffer, "status=");
    else
      strcat(msgBuffer, "&status=");
    strcat(msgBuffer, dataField);
  }
  // Note that ThingSpeak defines several other Payload Parameters beyond
  // field1-8. I have only implemented the "status" field, which is the 12th
  // paramter type in the API docs, hence, use "12" for status, even though
  // anything > 8 would work as is currently coded.
}

void BuildPayload(char* msgBuffer, char* dataFieldBuffer, int fieldNum, int data) {
  snprintf(dataFieldBuffer, FIELDBUFFERSIZE, "%d", data);
  BuildPayload(msgBuffer, fieldNum, dataFieldBuffer);
}

void BuildPayload(char* msgBuffer, char* dataFieldBuffer, int fieldNum, unsigned int data) {
  snprintf(dataFieldBuffer, FIELDBUFFERSIZE, "%u", data);
  BuildPayload(msgBuffer, fieldNum, dataFieldBuffer);
}

void BuildPayload(char* msgBuffer, char* dataFieldBuffer, int fieldNum, unsigned long data) {
  snprintf(dataFieldBuffer, FIELDBUFFERSIZE, "%lu", data);
  BuildPayload(msgBuffer, fieldNum, dataFieldBuffer);
}

void BuildPayload(char* msgBuffer, char* dataFieldBuffer, int fieldNum, const char* data) {
  snprintf(dataFieldBuffer, FIELDBUFFERSIZE, data);
  BuildPayload(msgBuffer, fieldNum, dataFieldBuffer);
}

#ifdef SKETCH_DEBUG
void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
#endif
