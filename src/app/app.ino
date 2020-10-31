#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

#define BREW_RECORD_ADDRESS 0

struct brewRecord_t
{
    float minTemp;
    float maxTemp;
};

struct request_t
{
    char mac_address[18];
    float beerTemp;
    float ambientTemp;
    bool heaterState;
    bool coolerState;
};

struct response_t
{
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
LiquidCrystal_I2C lcd(I2C_ADDR, 16, 2);

WiFiClient client;

brewRecord_t record;

const uint8_t heaterRelay = 15;
const uint8_t coolerRelay = 13;

String mac_address;

void setup()
{
    // Initialize pins responsible for controlling heater and cooler relays
    pinMode(heaterRelay, OUTPUT);
    digitalWrite(heaterRelay, LOW);
    pinMode(coolerRelay, OUTPUT);
    digitalWrite(coolerRelay, LOW);

    // Configure serial output for logging
    Serial.begin(115200);
    Serial1.println("Starting...");

    // Initialize LCD display
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("Starting...");

    // Start temperature sensors
    sensors.begin();

    // Initialize eeprom and retrieve stored record
    EEPROM.begin(512);
    EEPROM.get(BREW_RECORD_ADDRESS, record);

    // Configure wifi
    WiFi.hostname("microBrew-controller");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, psk);

    // Read MAC address
    mac_address = WiFi.macAddress();
    Serial.print("MAC address: ");
    Serial.println(mac_address);

    // Wait for wifi to connect
    int retry = 3;
    while (WiFi.status() != WL_CONNECTED && retry > 0)
    {
        delay(5000);
        retry--;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Couldn't connect to wifi");
        ESP.restart();
    }

    lcd.clear();
    lcd.print("Initialised");
    Serial.println("Initialised");
}

void loop()
{
    sensors.requestTemperatures();
    float beerTemp = sensors.getTempCByIndex(0);
    float ambientTemp = sensors.getTempCByIndex(1);
    bool heaterState = digitalRead(heaterRelay);
    bool coolerState = digitalRead(coolerRelay);
    Serial.printf("HeaterState: %d\n", heaterState);
    Serial.printf("CoolerState: %d\n", coolerState);
    Serial.printf("BeerTemp: %.2fC\n", beerTemp);
    Serial.printf("AmbientTemp: %.2fC\n", ambientTemp);

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("No wifi connection");
        manualControl(beerTemp);
        return;
    }

    if (!client.connect(host, port))
    {
        Serial.println("Connection to server failed");
        manualControl(beerTemp);
        return;
    }

    sendTemp(beerTemp, ambientTemp, heaterState, coolerState);
    handleResponse();

    printInfo(beerTemp, ambientTemp);

    client.stop();
    delay(30000);
}

void sendTemp(float beerTemp, float ambientTemp, bool heaterState, bool coolerState)
{
    struct request_t msg;
    mac_address.toCharArray(msg.mac_address, 18);
    msg.beerTemp = beerTemp;
    msg.ambientTemp = ambientTemp;
    msg.heaterState = heaterState;
    msg.coolerState = coolerState;

    byte *ptr_msg = (byte *)&msg;
    for (uint i = 0; i < sizeof(struct request_t); i++)
    {
        client.write(*ptr_msg++);
    }
    client.flush();
    Serial.println("Data sent");
}

void handleResponse()
{
    Serial.println("Online mode");
    int responseSize = sizeof(struct request_t);
    byte msgBuffer[responseSize];
    client.readBytes(msgBuffer, responseSize);

    response_t *msg = (response_t *)msgBuffer;

    // Update the brew id and temp range and persist it in the eeprom, if any of the values changed
    if (msg->minTemp != record.minTemp || msg->maxTemp != record.maxTemp)
    {
        Serial.println("Updating recorded temperatures");
        Serial.printf("New min temp: %.2f\n", msg->minTemp);
        Serial.printf("New max temp: %.2f\n", msg->maxTemp);
        record.minTemp = msg->minTemp;
        record.maxTemp = msg->maxTemp;

        EEPROM.put(BREW_RECORD_ADDRESS, record);
        EEPROM.commit();
        Serial.println("Record commited");
    }

    Serial.printf("Setting heater to %d\n", msg->heaterState);
    Serial.printf("Setting cooler to %d\n", msg->coolerState);
    digitalWrite(heaterRelay, msg->heaterState);
    digitalWrite(coolerRelay, msg->coolerState);
}

void manualControl(float currentTemp)
{
    // In case when we can't connect to the server we have to control the heater and cooler
    // based on current tempertarure and persisted min and max
    Serial.println("Offline mode");
    float minTemp = record.minTemp;
    float maxTemp = record.maxTemp;

    bool isNotAssigned = (minTemp == 0) && (maxTemp == 0);

    if (isNotAssigned)
    {
        Serial.println("Turning heater and cooler off");
        digitalWrite(coolerRelay, LOW);
        digitalWrite(heaterRelay, LOW);
        return;
    }

    float targetTemp = maxTemp - minTemp;
    if (currentTemp <= minTemp && currentTemp < targetTemp)
    {
        Serial.println("Turning heater on");
        digitalWrite(heaterRelay, HIGH);
        digitalWrite(coolerRelay, LOW);
        return;
    }
    if (currentTemp > maxTemp)
    {
        Serial.println("Turning cooler on");
        digitalWrite(coolerRelay, HIGH);
        digitalWrite(heaterRelay, LOW);
        return;
    }

    Serial.println("Turning heater and cooler off");
    digitalWrite(coolerRelay, LOW);
    digitalWrite(heaterRelay, LOW);
}

void printInfo(float beerTemp, float ambientTemp)
{
    lcd.clear();
    lcd.home();
    lcd.printf("%.1f-%.1f %.1f", record.minTemp, record.maxTemp, beerTemp);
    lcd.setCursor(0, 1);
    char heaterStatus = digitalRead(heaterRelay) ? 'H' : ' ';
    char coolerStatus = digitalRead(coolerRelay) ? 'C' : ' ';
    char wifiStatus = WiFi.status() == WL_CONNECTED ? 'W' : ' ';

    lcd.printf("%.1f %c %c %c", ambientTemp, heaterStatus, coolerStatus, wifiStatus);
}