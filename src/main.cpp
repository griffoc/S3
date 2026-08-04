#include <Arduino.h>
#include <ArduinoOTA.h>
#include <chrono>
#include <format>
#include <HTTPClient.h>
#include <Preferences.h>
#include <string>
#include <WiFi.h>
#include <Wire.h>
#include <INA226.h>

#define SDA_PIN 18
#define SCL_PIN 46

#define USE_OTA // Uncomment to enable OTA updates

INA226 ina226(0x40); // Create an instance of the INA226 class

void readCredentials(std::string&ssid, std::string& password);
void connectToWifi();
std::string CurrentTime();
void initOTA();
void printSystemTime();
void postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate);

void setup()
{
  Serial.begin(115200);
  //while(!Serial) { delay(10); } // Wait for serial monitor

  connectToWifi();
  
#ifdef USE_OTA
  initOTA();
#endif

  // Initialize I2C with the specified ESP32-S3 pins
  Wire.begin(SDA_PIN, SCL_PIN);
  
  //Initialize sensor module
  if(!ina226.begin())
  {
    Serial.println("INA226 not connected!");
    while(1)
      delay(1000);
  }

  ina226.setMaxCurrentShunt(20.0, 0.00375); // Set max current and shunt resistor value
}

void loop()
{
#ifdef USE_OTA
  ArduinoOTA.handle(); // Critical: Must run constantly to intercept network files
#endif
  static unsigned long previousmilliseconds = 0;
  const long interval = 1000;

  unsigned long milliseconds = millis();

  if (milliseconds - previousmilliseconds >= interval)
  {
    previousmilliseconds = milliseconds;

    float current = ina226.getCurrent(); // Get current in Amperes
    float power = ina226.getPower(); // Get power in Watts);
    float busVoltage = ina226.getBusVoltage(); // Get current in Amperes
    std::string measurementDate = CurrentTime();

    if(current < 0.0 || power < 0.0)
    {
      current = 0.0; // Ensure current is not negative
      power = 0.0; // Ensure power is not negative
    }

    if(busVoltage < 1.0)
    {
      rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
    }
    else
    {
      rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
    }

    postDataToServer(current, busVoltage, power, measurementDate);
  }
}

void readCredentials(std::string&ssid, std::string& password)
{
  Preferences preferences;

  // Open Preferences with 'credentials' namespace in R/W mode
  preferences.begin("credentials", false);

  // Read credentials back
  ssid = preferences.getString("ssid", "").c_str();
  password = preferences.getString("password", "").c_str();

  Serial.print("Stored SSID: ");
  Serial.println(ssid.c_str());
  
  // Always close the preferences when done
  preferences.end();

}

void connectToWifi()
{
  const char* ntpServer = "pool.ntp.org";

  std::string ssid;
  std::string password;
  readCredentials(ssid, password);

  Serial.print("Connecting to Wi-Fi");

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  // Set timezone and NTP server
  // Timezone format: TZ_OFFSET;DST_OFFSET,DST_START,DST_END
  configTime(0, 0, ntpServer); // 0 offset for UTC, adjust for local TZ

  Serial.println(".Connected to Wi-Fi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  printSystemTime(); // Print the current local time
}

std::string CurrentTime()
{
  return std::format("{:%Y-%m-%d%%20%T}", std::chrono::system_clock::now());
}

void initOTA()
{
  // Configure ArduinoOTA
  ArduinoOTA.setPort(3232); // Default port
  ArduinoOTA.setHostname("esp32s3-ota"); // Target network name
  ArduinoOTA.setPassword("pa44word123"); // Optional authentication

  ArduinoOTA.onStart([]() { Serial.println("Start OTA Update"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd OTA Update"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error){
    Serial.printf("Error[%u]: ", error);
  });

  ArduinoOTA.begin();
}

void printSystemTime()
{
  //WTF: getLocalTime must be called so that CurrentTime(), i.e chrono::system_clock::now(), returns
  //the correct time on the first call. Otherwise, 1970-01-01 00:00:00 is returned on the first few calls.
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.print("SystemTime: ");
  //Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.println(CurrentTime().c_str());
}

void postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWifi();
  }

  const char* serverUrl = "http://192.168.50.17/amps/store.php";
  
  // Example: Send data via HTTP POST with JSON
  std::string jsonData = std::format("{}?power={}&current={}&bus_voltage={}&measurement_date={}",
    serverUrl, power, current, busVoltage, measurementDate);

  HTTPClient http;
  http.begin(jsonData.c_str()); // Specify the URL
  
  Serial.println(jsonData.c_str());
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();  
}
