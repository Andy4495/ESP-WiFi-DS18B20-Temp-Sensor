/**
   WiFi connected temperature sensor
   https://github.com/Andy4495/ESP-WiFi-DS18B20-Temp-Sensor

   1.0 - 03/24/2019 - A.T. - Original.
   1.1 - 04/11/2019 - A.T. - Add error checking to DS18B20 readings
   1.2 - 08/23/2020 - A.T. - Add support for second DS18B20
   2.0 - 09/23/2021 - A.T. - Update to use Thingspeaks updated MQTT3 API in R2021a:
                             Use per-device authentication instead of user authentication
                             In addition to code changes and updated comments in this file,
                             API-specific changes were made in
                             MQTT_private_config.h and MQTT_private_feeds.h
   2.1 - 06/20/2022 - A.T. - Clarifications.
*/

/**
   Temperature sensor for fish pond and turtle pond.
   - Implemented on ESP8266 using SparkFun ESP8266 ThingDev
   - Uses 2x DS18B20 submergible temperature sensor
   - Connectes to ThingSpeak IoT platform using WiFi
**/
/**
   EXTERNAL LIBRARIES:
   - MQTT: https://github.com/adafruit/Adafruit_MQTT_Library
       - Adafruit_MQTT.cpp modified to comment out lines 425-431
         to remove support for floating point. Specifically,
         commented out the block starting with:
            "else if (sub->callback_double != NULL)"
*/

#define SKETCH_DEBUG   // Comment this line out to save RAM and program space.

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
#define MAX_RETRIES 20    // Retry attempts if error reading sensor

#include <ESP8266WiFi.h>
ADC_MODE(ADC_VCC);  // This macro is needed in order to use getVcc()
#include <OneWire.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "MQTT_private_config.h"
/* The MQTT_private_config.h file needs to include the following definitions
   specific to your configuration:
     byte mac[] = {6 byte MAC address for ethernet card};
     const char* ssid = "Your WiFi SSID";     // SSID of the WiFi network you will connectd to
     const char* password = "WiFi-password";  // Password to the WiFi network you will connect to
     
     #define TS_SERVER        "address of your MQTT server (e.g. mqtt3.thingspeak.com)"
     #define TS_SERVERPORT    Port number of your MQTT server, e.g. 1883
     #define TS_CLIENTID      "Client ID for your ThingSpeak MQTT device (typically same as Username)"
     #define TS_USERNAME      "Username for your ThingSpeak MQTT device"
     #define TS_KEY           "Password for your ThingSpeak MQTT device"
   Also, #define CHANNEL_IDs for each of your sensors
*/
WiFiClient client_ts;
Adafruit_MQTT_Client thingspeak(&client_ts, TS_SERVER, TS_SERVERPORT, TS_CLIENTID, TS_USERNAME, TS_KEY);
#define PAYLOADSIZE 132
char payload[PAYLOADSIZE];    // MQTT payload string
#define FIELDBUFFERSIZE 20
char fieldBuffer[FIELDBUFFERSIZE];  // Temporary buffer to construct a single field of payload string

struct PondData {
  int             MSP_T;          // Tenth degrees F
  int             Submerged_T;    // Tenth degrees F
  unsigned int    Batt_mV;        // milliVolts
  int             WiFi_RSSI;      // --> Redefined to indicate WiFi RSSI
  int             Aerator_Status; // Unimplemented
  unsigned long   Loops;          // --> Redefined to indicate number of times through loop()
  int             Battery_T;      // Tenth degrees F
};

// -----------------------------------------------------------------------------
/**
    Global data
*/

PondData ponddata;
int lostConnectionCount = 0;
unsigned long numberOfLoops = 0;
int gettemp_reset_count = 0;
int readscratch_reset_count = 0;
int crc_count = 0;
int fish_read_error_count;
int turtle_read_error_count;

OneWire  ds18b20(DS18B20_SIGNAL_PIN);  // Requires a 4.7K pull-up resistor
byte fishThermometer[8]     = { 0x28, 0x95, 0x5C, 0x37, 0x0A, 0x00, 0x00, 0x54 };
byte turtleThermometer[8]   = { 0x28, 0xDC, 0xAC, 0x88, 0x0A, 0x00, 0x00, 0x1A };
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
   The file "MQTT_publishing_feeds.h" needs to include the feed/channel definitions
   specific to your configuration.
*/
#include "MQTT_publishing_feeds.h"

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
  MQTT_connect();

  digitalWrite(BOARD_LED, HIGH);
  delay(500);
  digitalWrite(BOARD_LED, LOW);

  set_resolution_10_bit(fishThermometer);
  set_resolution_10_bit(turtleThermometer);
}  // setup()

void loop()
{
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).
  MQTT_connect();

  process_fishdata();
  process_turtledata();
  build_MQTT_message();

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

void setResolution(uint8_t resolution, byte* address) {
  ds18b20.reset();
  ds18b20.select(address);
  ds18b20.write(WRITESCRATCH);             // no parasitic power (2nd argument defaults to zero)

  scratchpad[CONFIGURATION] = resolution;  // Set the resolution value. Don't care about TH and TL, so don't bother setting.

  for (int i = HIGH_ALARM_TEMP; i <= CONFIGURATION; i++) {  // 3 bytes required for the WRITESCRATCH command
    ds18b20.write(scratchpad[i]);
  }

  ds18b20.reset();
  ds18b20.select(address);
  ds18b20.write(COPYSCRATCH);              // no parasitic power (2nd argument defaults to zero)

  delay(15);                               // Need a minimum of 10ms per datasheet after copy scratch
}

void process_fishdata() {
  int16_t celsius, fahrenheit;
  gettemp_reset_count = 0;
  readscratch_reset_count = 0;
  crc_count = 0;
  fish_read_error_count = 0;

  // Clear out the scratchpad
  memset(scratchpad, 0, 9);

  // Read the temperature from the DS18B20
  // Start temperature conversion
  while (crc_count < MAX_RETRIES) {
    gettemp_reset_count = 0;
    while ((ds18b20.reset() == 0) && (gettemp_reset_count < MAX_RETRIES)) {
      gettemp_reset_count++;
    }
    ds18b20.select(fishThermometer);
    ds18b20.write(GETTEMP);        // no parasitic power (2nd argument defaults to zero)
    delay(225);     // 10-bit needs 187.5 ms for conversion, add a little extra for clock inaccuracy

    // Read back the temperature
    while ((ds18b20.reset() == 0) && (readscratch_reset_count < MAX_RETRIES)) {
      readscratch_reset_count++;
    }
    ds18b20.select(fishThermometer);
    ds18b20.write(READSCRATCH);
    for ( int i = 0; i < 9; i++) {           // Only need 2 bytes to get the temperature; read all 9 bytes to calculate CRC
      scratchpad[i] = ds18b20.read();
    }
    if (OneWire::crc8(scratchpad, 8) == scratchpad[8]) break;  // Break out of while loop if CRC matches
    else crc_count++;
  } // while (crc_count < MAX_RETRIES)

  // Convert the data to actual temperature
  int16_t raw = (scratchpad[1] << 8) | scratchpad[0]; // Put the temp bytes into a 16-bit integer
  raw = raw & ~0x03;               // 10-bit resolution, so ignore 2 lsb
  // Raw result is in 16ths of a degree Celsius
  // fahrenheit = celsius * 1.8 + 32.0, but we want to use integer math
  celsius = (raw * 10) >> 4;                   // Convert to 10th degree celsius
  fahrenheit = ((celsius * 9) / 5) + 320;      // C to F using integer math (values are in tenth degrees)
  ponddata.Submerged_T = fahrenheit;
  fish_read_error_count = gettemp_reset_count + readscratch_reset_count + crc_count;
} // process_fishdata()

void process_turtledata() {
  int16_t celsius, fahrenheit;
  gettemp_reset_count = 0;
  readscratch_reset_count = 0;
  crc_count = 0;
  turtle_read_error_count = 0;

  // Clear out the scratchpad
  memset(scratchpad, 0, 9);

  // Read the temperature from the DS18B20
  // Start temperature conversion
  while (crc_count < MAX_RETRIES) {
    gettemp_reset_count = 0;
    while ((ds18b20.reset() == 0) && (gettemp_reset_count < MAX_RETRIES)) {
      gettemp_reset_count++;
    }
    ds18b20.select(turtleThermometer);
    ds18b20.write(GETTEMP);        // no parasitic power (2nd argument defaults to zero)
    delay(225);     // 10-bit needs 187.5 ms for conversion, add a little extra for clock inaccuracy

    // Read back the temperature
    while ((ds18b20.reset() == 0) && (readscratch_reset_count < MAX_RETRIES)) {
      readscratch_reset_count++;
    }
    ds18b20.select(turtleThermometer);
    ds18b20.write(READSCRATCH);
    for ( int i = 0; i < 9; i++) {           // Only need 2 bytes to get the temperature; read all 9 bytes to calculate CRC
      scratchpad[i] = ds18b20.read();
    }
    if (OneWire::crc8(scratchpad, 8) == scratchpad[8]) break;  // Break out of while loop if CRC matches
    else crc_count++;
  } // while (crc_count < MAX_RETRIES)

  // Convert the data to actual temperature
  int16_t raw = (scratchpad[1] << 8) | scratchpad[0]; // Put the temp bytes into a 16-bit integer
  raw = raw & ~0x03;               // 10-bit resolution, so ignore 2 lsb
  // Raw result is in 16ths of a degree Celsius
  // fahrenheit = celsius * 1.8 + 32.0, but we want to use integer math
  celsius = (raw * 10) >> 4;                   // Convert to 10th degree celsius
  fahrenheit = ((celsius * 9) / 5) + 320;      // C to F using integer math (values are in tenth degrees)
  ponddata.MSP_T = fahrenheit;
  turtle_read_error_count = gettemp_reset_count + readscratch_reset_count + crc_count;
} // process_turtledata()

void build_MQTT_message() {

  ponddata.WiFi_RSSI = (int) WiFi.RSSI();
  ponddata.Aerator_Status = 0;
  ponddata.Loops = numberOfLoops;
  ponddata.Battery_T = 0;

  SKETCH_PRINTLN(F("Sending pond sensor info. "));
  SKETCH_PRINT("WiFi RSSI: ");
  SKETCH_PRINTLN(ponddata.WiFi_RSSI);
  SKETCH_PRINT(F("Fish Temperature (F): "));
  SKETCH_PRINT(ponddata.Submerged_T / 10);
  SKETCH_PRINT(F("."));
  SKETCH_PRINTLN(ponddata.Submerged_T % 10);
  SKETCH_PRINT(F("Turtle Temperature (F): "));
  SKETCH_PRINT(ponddata.MSP_T / 10);
  SKETCH_PRINT(F("."));
  SKETCH_PRINTLN(ponddata.MSP_T % 10);
  ponddata.Batt_mV = ESP.getVcc();
  SKETCH_PRINT("ESP8266 Vcc: ");
  SKETCH_PRINTLN(ponddata.Batt_mV);

  payload[0] = '\0';
  BuildPayload(payload, fieldBuffer, 1, ponddata.MSP_T);
  BuildPayload(payload, fieldBuffer, 2, ponddata.Submerged_T);
  BuildPayload(payload, fieldBuffer, 3, ponddata.Batt_mV);
  BuildPayload(payload, fieldBuffer, 4, ponddata.Loops);
  BuildPayload(payload, fieldBuffer, 5, ponddata.WiFi_RSSI);
  BuildPayload(payload, fieldBuffer, 6, ponddata.Aerator_Status);
  BuildPayload(payload, fieldBuffer, 7, ponddata.Battery_T);
  if ( (fish_read_error_count > 0) || (turtle_read_error_count > 0) ) {
    BuildPayload(payload, fieldBuffer, 12, "Fish, Turt: ");
    snprintf(fieldBuffer, FIELDBUFFERSIZE, "%d, %d", fish_read_error_count, turtle_read_error_count);
    strcat(payload, fieldBuffer);
  }
  SKETCH_PRINTLN(F("Sending data to ThingSpeak..."));
  SKETCH_PRINT(F("Payload: "));
  SKETCH_PRINTLN(payload);
  if (! Pond_Sensor.publish(payload)) {
    SKETCH_PRINTLN(F("Pond_Sensor Channel Failed to ThingSpeak."));
  }
} // build_MQTT_message()

void set_resolution_10_bit(byte* address) {

  // Read scratchpad to get current resolution value
  ds18b20.reset();
  ds18b20.select(address);                    // Only one device on the bus, so don't need to bother with the address
  ds18b20.write(READSCRATCH);        // no parasitic power (2nd argument defaults to zero)

  for (int i = 0; i < 9; i++) {      // Read all 9 bytes
    scratchpad[i] = ds18b20.read();
  }

  switch (scratchpad[CONFIGURATION]) {
    case TEMP_9_BIT:
      setResolution(TEMP_10_BIT, address);         // Change resolution to 10 bits
      break;
    case TEMP_10_BIT:
      break;
    case TEMP_11_BIT:
      setResolution(TEMP_10_BIT, address);         // Change resolution to 10 bits
      break;
    case TEMP_12_BIT:
      setResolution(TEMP_10_BIT, address);         // Change resolution to 10 bits
      break;
    default:
      // Note: Unexpected CONFIG value
      break;
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void MQTT_connect() {

  // Return if already connected.
  if (thingspeak.connected()) {
    return;
  }

  SKETCH_PRINTLN(F("MQTT Disconnected."));

  SKETCH_PRINT(F("Attempting reconnect to MQTT: "));
  SKETCH_PRINTLN(millis());
  thingspeak.connect();
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
