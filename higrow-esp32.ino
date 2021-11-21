#include <DHTesp.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ESP.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <HTTPUpdate.h>

// This is inspired from https://github.com/lucafabbri/HiGrow-Arduino-Esp

#define VERSION_MAJOR 0
#define VERSION_MINOR 2
#define VERSION_PATCH 4
#define VERSION_INT (VERSION_MAJOR*10000+VERSION_MINOR*100+VERSION_PATCH)

// Pin layout
#define PIN_LED_BLUE  16
#define PIN_DHT       22
#define PIN_SOIL      32
#define PIN_POWER     34
#define PIN_LIGHT     33

#define EEDOMUS_API_KEY "oQ9DLF"

// For secrets
#define EEDOMUS_API_SECRET "********"
#define OTAP_URL_SECRET "********"
#define WIFI_PASSWORD_1 "********"
#define WIFI_PASSWORD_2 "********"
#define WIFI_PASSWORD_3 "********"
#define WIFI_PASSWORD_4 "********"

#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))

void pinsSetup() {
  pinMode(PIN_POWER, INPUT); // No idea what this does

  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_BLUE, LOW);

  pinMode(PIN_LIGHT, INPUT);
}

void serialSetup() {
  Serial.begin(115200);
  delay(10);
  Serial.printf("Wifi Soil Moisture Sensor v%d.%d.%d (%s %s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, __DATE__, __TIME__);
}

typedef struct {
  String name;
  String macAddr;
  int deviceIdSoil;
  int deviceIdAirHumidity;
  int deviceIdAirTemperature;
} t_device_ids;

// This is where I list all my sensors
t_device_ids ids[] = {
  { .name = "C1", .macAddr = "30:AE:A4:F4:C9:2C", .deviceIdSoil = 1579253 },
  { .name = "C2", .macAddr = "CC:50:E3:A8:6B:64", .deviceIdSoil = 1579248 },
  { .name = "C3", .macAddr = "CC:50:E3:A8:72:EC", .deviceIdSoil = 1579249 },
  { .name = "C4", .macAddr = "30:AE:A4:F3:48:80", .deviceIdSoil = 1579250 },
  { .name = "C5", .macAddr = "CC:50:E3:A8:97:54", .deviceIdSoil = 1584398 },
  { .name = "C6", .macAddr = "CC:50:E3:A8:6E:48", .deviceIdSoil = 1579254 }
};

t_device_ids * current_device_ids = NULL;
Preferences preferences;

// char deviceid[21] = "";
void settingsSetup() {
  String macAddress = WiFi.macAddress();
  for ( int i = 0; i < NELEMS(ids); i++ ) {
    t_device_ids * dev_id = & ids[i];
    if ( dev_id->macAddr == macAddress ) {
      current_device_ids = dev_id;
      Serial.print("Device identified: " + dev_id->name + "\n" );
      return;
    }
  }
  Serial.print("We don't know this device (" + macAddress + ")\n");
  sleepGo();
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
  Serial.print("Connecting Wifi...");
  if ( current_device_ids ) {
    WiFi.setHostname(current_device_ids->name.c_str());
  }
  WiFi.enableSTA(true);
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println(String("WiFi connected to \"") + WiFi.SSID() + "\"");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void wifiSetup() {
  wifiSetAPs();
  wifiConnect();
}

// TODO: The whole coming back from sleep logic is broken (bot for the RTC-attached memory and wake up reason). I have no idea of why.
void sleepWakeUpReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    case 0: break;
    default : Serial.printf("Unknwon wake up reason: %d\n", wakeup_reason); break;
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
  // It shouldn't take more than 5 minute to run or even reflash the program
  timerAttachInterrupt(watchdogTimer, &watchdogReset, true);
  timerAlarmWrite(watchdogTimer, 5L * 60 * 1000 * 1000, false);
  timerAlarmEnable(watchdogTimer);
}

void watchdogDisable() {
  timerAlarmDisable(watchdogTimer);
  timerEnd(watchdogTimer);
}

void otapSetup() {
  const int r = esp_random() % 1;
  if ( r != 0 ) {
    Serial.println("OTAP: Skipping it this time...");
    return;
  }

  {
    HTTPClient client;
    client.begin(String(OTAP_URL_SECRET) + "version.txt");
    int status = client.GET();
    if (status != 200) {
      Serial.printf("OTAP: Wrong HTTP status: %d\n", status);
      return;
    }
    int version = client.getString().toInt();
    if ( version <= VERSION_INT ) {
      Serial.printf("OTAP: No update available (%d <= %d)\n", version, VERSION_INT);
      return;
    }
    Serial.printf("OTAP: Updating from %d to %d\n", VERSION_INT, version);
  }
  {
    Serial.println("OTAP: Starting update");
    WiFiClient client;

    // The line below is optional. It can be used to blink the LED on the board during flashing
    // The LED will be on during download of one buffer of data from the network. The LED will
    // be off during writing that buffer to flash
    // On a good connection the LED should flash regularly. On a bad connection the LED will be
    // on much longer than it will be off. Other pins than LED_BUILTIN may be used. The second
    // value is used to put the LED on. If the LED is on with HIGH, that value should be passed
    // httpUpdate.setLedPin(LED_BUILTIN, LOW);

    t_httpUpdate_return ret = httpUpdate.update(client, String(OTAP_URL_SECRET) + "file.bin");

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("OTAP: Failed: Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("OTAP: No update");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("OTAP: OK");
        break;
    }
  }
}

void setup() {
  serialSetup();

  // We need a watchdog because the WifiMulti module is extremely buggy. It can hang forever if it doesn't find a network immediately.
  watchdogSetup();
  sleepSetup();
  pinsSetup();
  settingsSetup();
  wifiSetup();
  otapSetup();
}

// Sensors used
typedef struct {
  float airHumidity;
  float airTemperature;
  float soilMoisture;
  //float light;
} t_sensors;

t_sensors sensors = {};

// Converts a raw value into a percentage
float toPercentage(int value, int valueMin, int valueMax, bool revert, String type) {
  {
    char prefName[100];
    sprintf(prefName, "per.%s", type);
    preferences.begin(prefName, false);
  }
  valueMin = preferences.getInt("min", valueMin);
  valueMax = preferences.getInt("max", valueMax);
  if ( value > valueMax ) {
    valueMax = value;
    Serial.printf("Saving new max: %d\n", valueMax);
    preferences.putInt("max", valueMax);
  } else if ( value < valueMin ) {
    valueMin = value;
    Serial.printf("Saving new min: %d\n", valueMin);
    preferences.putInt("min", valueMin);
  }
  preferences.end();

  Serial.printf("Value: %d, Min: %d, Max: %d\n", value, valueMin, valueMax);

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
  const int nb_readings = 20;
  const int measures_time = 30000;
  int soilMoistureLevel = 0;

  for( int i = 0 ; i < nb_readings; i++ ) {
    int reading = analogRead(PIN_SOIL);
    soilMoistureLevel += reading;
    Serial.printf("Soil moisture (raw): %d (%d)\n", reading, i);
    delay(measures_time/nb_readings);
  }
  soilMoistureLevel /= nb_readings;
  Serial.printf("Soil moisture (raw): %d\n", soilMoistureLevel);

  sensors.soilMoisture = toPercentage(soilMoistureLevel, 1100, 3000, true, "soil");

  Serial.printf("Soil moisture (corrected): %.2f%%\n", sensors.soilMoisture);
}

void sensorsFetch() {
  sensorsFetchDht();
  sensorsFetchAdc();
}

void eedomusSend(int periphId, String value) {
  if ( ! periphId ) {
    return;
  }
  Serial.print(String("Eedomus: Setting \"") + value + "\" for periphId " + periphId + "... ");
  String url = String("http://api.eedomus.com/set?api_user=") + EEDOMUS_API_KEY + "&api_secret=" + EEDOMUS_API_SECRET + "&action=periph.value&periph_id=" + periphId + "&value=" + value;

  for ( int i = 0; i < 3; i++ ) {
    HTTPClient client;
    client.begin(url);
    int status = client.GET();
    Serial.printf("Done (%d)\n", status);
    if ( status == 200 ) {
      break;
    }
  }
}

void eedomusSend(int periphId, float value) {
  char format[100];
  sprintf(format, "%.2f", value);
  eedomusSend(periphId, format);
}

void sendSensorsData() {
  sensorsFetch();

  if ( ! current_device_ids ) {
    Serial.print("No device set !\n");
    return;
  }

  eedomusSend(current_device_ids->deviceIdSoil, sensors.soilMoisture);
  eedomusSend(current_device_ids->deviceIdAirHumidity, sensors.airHumidity);
  eedomusSend(current_device_ids->deviceIdAirTemperature, sensors.airTemperature);
}

void loop() {
  sendSensorsData();
  watchdogDisable();
  sleepGo();
}
