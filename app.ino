#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define BREW_RECORD_ADDRESS 0

struct brewRecord_t
{
    uint32_t brewId;
    float minTemp;
    float maxTemp;
};

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
    float minTemp;
    float maxTemp;
    bool heaterState;
    bool coolerState;
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

WiFiClient client;

brewRecord_t record;

const uint8_t heaterRelay = 5;
const uint8_t coolerRelay = 4;

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

    // Initialize oneWire library
    sensors.begin();

    // Initialize eeprom
    EEPROM.begin(512);
    EEPROM.get(BREW_RECORD_ADDRESS, record);

    // Initialize mins responsible for controlling heater and cooler relays
    pinMode(heaterRelay, OUTPUT);
    pinMode(coolerRelay, OUTPUT);
}

void loop()
{
    sensors.requestTemperatures();
    float beerTemp = sensors.getTempCByIndex(0);
    float ambientTemp = -99.9; //sensors.getTempCByIndex(1);

    Serial.printf("Beer temp: %.2fºC\n", beerTemp);
    Serial.printf("Ambient temp: %.2fºC\n", ambientTemp);

    if (!client.connect(host, port))
    {
        Serial.println("Connection to server failed");
        manualControl(beerTemp);
        return;
    }

    sendTemp(beerTemp, ambientTemp);
    handleResponse();

    client.stop();
    delay(5000);
}

void sendTemp(float beerTemp, float ambientTemp)
{
    bool heaterState = digitalRead(heaterRelay);
    bool coolerState = digitalRead(coolerRelay);

    struct request_t msg;
    msg.brewId = 1;
    msg.beerTemp = beerTemp;
    msg.ambientTemp = ambientTemp;
    msg.heaterState = heaterState;
    msg.coolerState = coolerState;

    byte *ptr_msg = (byte *)&msg;
    for (uint i = 0; i < sizeof(struct request_t); i++)
    {
        client.write(*ptr_msg++);
    }
}

void handleResponse()
{
    int responseSize = sizeof(struct request_t);
    byte msgBuffer[responseSize];
    client.readBytes(msgBuffer, responseSize);

    response_t *msg = (response_t *)msgBuffer;

    // Update the brew id and temp range and persist it in the eeprom, if any of the values changed
    if (msg->brewId != record.brewId || msg->minTemp != record.minTemp || msg->maxTemp != record.maxTemp)
    {
        record.brewId = msg->brewId;
        record.minTemp = msg->minTemp;
        record.maxTemp = msg->maxTemp;

        EEPROM.put(BREW_RECORD_ADDRESS, record);
        EEPROM.commit();
        Serial.println("Record commited");
    }

    digitalWrite(heaterRelay, msg->heaterState);
    digitalWrite(coolerRelay, msg->coolerState);
}

void manualControl(float currentTemp)
{
    // In case when we can't connect to the server we have to control the heater and cooler
    // based on current tempertarure and persisted min and max
    float minTemp = record.minTemp;
    float maxTemp = record.maxTemp;

    float targetTemp = maxTemp - minTemp;
    if (currentTemp <= minTemp && currentTemp < targetTemp)
    {
        digitalWrite(heaterRelay, HIGH);
        digitalWrite(coolerRelay, LOW);
        return;
    }
    else if (currentTemp > maxTemp)
    {
        digitalWrite(coolerRelay, HIGH);
        digitalWrite(heaterRelay, LOW);
        return;
    }

    digitalWrite(coolerRelay, LOW);
    digitalWrite(heaterRelay, LOW);
}