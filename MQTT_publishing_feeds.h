/* MQTT publishing feeds
     Each feed that you wish to publish needs to be defined below.
     - ThingSpeak Channel topics follow the form: channels/<CHANNEL_ID>/publish, for example:
         Adafruit_MQTT_Publish myChannel = Adafruit_MQTT_Publish(&mqtt,
                                        "channels/" CHANNEL_ID "/publish");
         See https://www.mathworks.com/help/thingspeak/publishtoachannelfeed.html
         See https://www.mathworks.com/help/thingspeak/release-notes.html for changes to the MQTT3
         interface included in R2021a release. The key change is that instead of using a per-channel
         API key, each publishing device has it's own device credentials.
*/

// New Thingspeak format
Adafruit_MQTT_Publish Pond_Sensor = Adafruit_MQTT_Publish(&thingspeak,
                                     "channels/" TS_POND_CHANNEL_ID "/publish");
