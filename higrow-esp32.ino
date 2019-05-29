#include <DHTesp.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ESP.h>

// This is inspired from https://github.com/lucafabbri/HiGrow-Arduino-Esp

#define VERSION "0.1"

// Pin layout
#define PIN_LED_BLUE  16
#define PIN_DHT       22
#define PIN_SOIL      32
#define PIN_POWER     34
#define PIN_LIGHT     33

#define EEDOMUS_API_KEY "oQ9DLF"

// For secrets
#define EEDOMUS_API_SECRET "********"
#define WIFI_PASSWORD_1 "********"
#define WIFI_PASSWORD_2 "********"
#define WIFI_PASSWORD_3 "********"
#define WIFI_PASSWORD_4 "********"

void pinsSetup() {
  pinMode(PIN_POWER, INPUT); // No idea what this does

  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_BLUE, LOW);

  pinMode(PIN_LIGHT, INPUT);
}

void serialSetup() {
  Serial.begin(115200);
  delay(10);
  Serial.printf("Wifi Soil Moisture Sensor (%s / %s %s)\n", VERSION, __DATE__, __TIME__);
}

char deviceid[21] = "";
void settingsSetup() {
  sprintf(deviceid, "%" PRIu64, ESP.getEfuseMac());
  Serial.print("DeviceId: ");
  Serial.println(deviceid);
}

WiFiMulti wifiMulti;
void wifiSetAPs() {
  wifiMulti.addAP("happynet", WIFI_PASSWORD_1 );
  wifiMulti.addAP("happynet2", WIFI_PASSWORD_1);
  wifiMulti.addAP("12eNbPPlantes", WIFI_PASSWORD_2);
  wifiMulti.addAP("SuperPernety", WIFI_PASSWORD_3);
  wifiMulti.addAP("habx", WIFI_PASSWORD_4);
}

void wifiConnect() {
    Serial.print(String("MAC address: ")+WiFi.macAddress());
    Serial.println();

    WiFi.setHostname("wifi_gms");

    Serial.print("Connecting Wifi...");
    WiFi.mode(WIFI_STA);
    while (wifiMulti.run() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("");
    Serial.println(String("WiFi connected to \"")+WiFi.SSID()+"\"");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void wifiSetup() {
  wifiSetAPs();
  wifiConnect();
}

// TODO: The whole coming back from sleep logic is broken (bot for the RTC-attached memory and wake up reason). I have no idea of why.
void sleepWakeUpReason(){
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    case 0: break;
    default : Serial.printf("Unknwon wake up reason: %d\n",wakeup_reason); break;
  }
}

RTC_DATA_ATTR int bootCount = 0;

void sleepSetup() {
  ++bootCount;
  Serial.printf("Boot number: %d\n", bootCount);
  sleepWakeUpReason();
}

void sleepGo() {
  const int sleepPeriod = 900;

  WiFi.mode(WIFI_OFF);
  Serial.printf("Going to sleep for %d s (after running for %d ms)...\n", sleepPeriod, millis());
  Serial.flush();
  esp_sleep_enable_timer_wakeup(1000000 /* uS_TO_S_FACTOR */ * sleepPeriod );
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_deep_sleep_start();
}

hw_timer_t *watchdogTimer = NULL;
void IRAM_ATTR watchdogReset() {
  Serial.printf("Watchdog timeout !\n");
  esp_restart();
}

void watchdogSetup() {
  watchdogTimer = timerBegin(0, 80, true);
  // It shouldn't take more than 1 minute to run the program
  timerAttachInterrupt(watchdogTimer, &watchdogReset, true);
  timerAlarmWrite(watchdogTimer, 1L * 60 * 1000 * 1000, false);
  timerAlarmEnable(watchdogTimer);
}

void watchdogDisable() {
  timerAlarmDisable(watchdogTimer);
  timerEnd(watchdogTimer);
}

void setup() {
    serialSetup();

    // We need a watchdog because the WifiMulti module is extremely buggy. It can hang forever if it doesn't find a network immediately.
    watchdogSetup();
    sleepSetup();
    pinsSetup();
    settingsSetup();
    wifiSetup();
}

// Sensors used
typedef struct {
  float airHumidity;
  float airTemperature;
  float soilMoisture;
  float light;
} t_sensors;

t_sensors sensors = {};

// Converts a raw value into a percentage
float toPercentage(int value, const int valueMin, const int valueMax, bool revert ) {
  float per = 100.0 * (value - valueMin) / (valueMax - valueMin);
  if ( revert ) {
    per = 100 - per;
  }
  return per;
}

void sensorsFetchDht () {
  DHTesp dht;
  dht.setup(PIN_DHT, DHTesp::DHT11);
  sensors.airHumidity = dht.getHumidity();
  sensors.airTemperature = dht.getTemperature();

  Serial.printf("Temperature: %f\n", sensors.airTemperature);
  Serial.printf("Humidity: %f\n", sensors.airHumidity);
}

void sensorsFetchAdc () { // Fetching data from ADC
  int soilMoistureLevel = analogRead(PIN_SOIL);
  int lightLevel = analogRead(PIN_LIGHT);

  Serial.printf("Soil moisture (raw): %d\n", soilMoistureLevel);
  Serial.printf("Light (raw): %d\n", lightLevel);

  sensors.soilMoisture = toPercentage(soilMoistureLevel, 1400, 3250, true);
  sensors.light = lightLevel; // Light gives completely bogus data

  Serial.printf("Soil moisture (corrected): %.2f%%\n", sensors.soilMoisture);
}

void sensorsFetch() {
  sensorsFetchDht();
  sensorsFetchAdc();
}

void eedomusSend(int periphId, String value) {
  Serial.print(String("Eedomus: Setting \"")+ value+"\" for periphId "+ periphId + "... ");
  String url = String("http://api.eedomus.com/set?api_user=")+EEDOMUS_API_KEY+"&api_secret="+EEDOMUS_API_SECRET+"&action=periph.value&periph_id="+periphId+"&value="+value;

  HTTPClient client;
  client.begin(url);
  int status = client.GET();
  Serial.printf("Done (%d)\n", status);
}

void sendSensorsData() {
  sensorsFetch();
  eedomusSend(1486564, String(int(sensors.soilMoisture)));
  eedomusSend(1486569, String(int(sensors.light)));
  eedomusSend(1488106, String(int(sensors.airHumidity)));
  eedomusSend(1488113, String(int(sensors.airTemperature)));
}

void loop() {
  sendSensorsData();
  watchdogDisable();
  sleepGo();
}
