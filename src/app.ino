#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

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

// Setup LCD
#define I2C_ADDR 0x27
#define WIFI_MESSAGE "Waiting for wifi"
LiquidCrystal_I2C lcd(I2C_ADDR, 16, 2);

WiFiClient client;

brewRecord_t record;

const uint8_t heaterRelay = 15;
const uint8_t coolerRelay = 13;

void setup()
{
    // Initialize LCD display
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print(WIFI_MESSAGE);

    // Start serial output for logging
    Serial.begin(115200);

    // Configure wifi
    WiFi.hostname("microBrew-controller");
    WiFi.begin(ssid, psk);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println(WIFI_MESSAGE);
    }

    lcd.clear();
    sensors.begin();

    // Initialize eeprom and retrieve stored record
    EEPROM.begin(512);
    EEPROM.get(BREW_RECORD_ADDRESS, record);

    // Initialize pins responsible for controlling heater and cooler relays
    pinMode(heaterRelay, OUTPUT);
    digitalWrite(heaterRelay, LOW);
    pinMode(coolerRelay, OUTPUT);
    digitalWrite(coolerRelay, LOW);
}

void loop()
{
    sensors.requestTemperatures();
    float beerTemp = sensors.getTempCByIndex(0);
    float ambientTemp = sensors.getTempCByIndex(1);
    bool heaterState = digitalRead(heaterRelay);
    bool coolerState = digitalRead(coolerRelay);

    printTemps(beerTemp, ambientTemp);
    if (!client.connect(host, port))
    {
        Serial.println("Connection to server failed");
        manualControl(beerTemp);
        return;
    }

    sendTemp(beerTemp, ambientTemp, heaterState, coolerState);
    handleResponse();

    client.stop();
    delay(5000);
}

void sendTemp(float beerTemp, float ambientTemp, bool heaterState, bool coolerState)
{
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

void printTemps(float beerTemp, float ambientTemp)
{
    Serial.printf("Beer temp: %.2fC\n", beerTemp);
    Serial.printf("Ambient temp: %.2fC\n", ambientTemp);

    lcd.home();
    lcd.printf("Beer:    %.2f", beerTemp);
    lcd.setCursor(0, 1);
    lcd.printf("Ambient: %.2f", ambientTemp);
}
