#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define RESPONSE_SIZE 16
#define BREW_RECORD_ADDRESS 0

struct brewRecord_t
{
    uint32_t brewId;
    float minTemp;
    float maxTemp;
} ;

struct request_t
{
    uint32_t brewId;
    float beerTemp;
    float ambientTemp;
    bool heaterState;
    bool coolerState;
};

struct response_t
{
    uint32_t brewId;
    bool heaterState;
    bool coolerState;
    float minTemp;
    float maxTemp;
};

// Populate wifi credentials
const char *ssid = "$$$$";
const char *psk = "$$$$";

// Populate server details
const char *host = "$$$$";
const uint16_t port = 52100;

const int oneWireGpio = 2;
OneWire oneWire(oneWireGpio);
DallasTemperature sensors(&oneWire);

bool heaterState = false;
WiFiClient client;

void setup()
{
    // Start serial output for logging
    Serial.begin(115200);

    // Configure wifi
    WiFi.hostname("microBrew-controller");
    WiFi.begin(ssid, psk);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println("Waiting for wifi connection");
    }

    sensors.begin();

    EEPROM.begin(512);
}

void loop()
{
    sensors.requestTemperatures();
    float beerTemp = sensors.getTempCByIndex(0);
    float ambientTemp = -99.9; //sensors.getTempCByIndex(1);

    Serial.printf("Beer temp: %.2fºC\n", beerTemp);
    Serial.printf("Ambient temp: %.2fºC\n", ambientTemp);

    // client.setNoDelay(true);
    if (!client.connect(host, port))
    {
        Serial.println("Connection to server failed");
        return;
    }

    sendTemp(beerTemp, ambientTemp);
    receiveResponse();

    client.stop();
    delay(5000);
}

float sendTemp(float beerTemp, float ambientTemp)
{
    struct request_t msg;
    msg.brewId = 1;
    msg.beerTemp = beerTemp;
    msg.ambientTemp = ambientTemp;
    msg.heaterState = heaterState;
    msg.coolerState = false;

    byte *ptr_msg = (byte *)&msg;
    for (uint i = 0; i < sizeof(struct request_t); i++)
    {
        client.write(*ptr_msg++);
    }
}

void receiveResponse()
{
    byte msgBuffer[RESPONSE_SIZE];
    client.readBytes(msgBuffer, RESPONSE_SIZE);

    response_t *msg = (response_t *)msgBuffer;
    brewRecord_t record;

    EEPROM.get(BREW_RECORD_ADDRESS, record);
    if (msg->brewId != record.brewId || msg->minTemp != record.minTemp || msg->maxTemp != record.maxTemp)
    {
        record.brewId = msg->brewId;
        record.minTemp = msg->minTemp;
        record.maxTemp = msg->maxTemp;

        EEPROM.put(BREW_RECORD_ADDRESS, record);
        EEPROM.commit();
        Serial.println("Record commited");
    }
}