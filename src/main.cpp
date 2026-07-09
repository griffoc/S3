#include <Arduino.h>
#include <ArduinoOTA.h>
#include <chrono>
#include <format>
#include <HTTPClient.h>
#include <string>
#include <WiFi.h>
#include <Wire.h>
#include <INA226.h>

#define SDA_PIN 18
#define SCL_PIN 46

#define USE_OTA // Uncomment to enable OTA updates

INA226 ina226(0x40); // Create an instance of the INA226 class

void connectToWifi();
std::string CurrentTime();
void initOTA();
void printSystemTime();
void postDataToServer(float current, float busVoltage, float shuntVoltage, float power, const std::string& measurementDate);
void printINA226Values(float current, float busVoltage, float shuntVoltage, float power, const std::string& measurementDate);

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
  const long interval = 200;

  unsigned long milliseconds = millis();

  if (milliseconds - previousmilliseconds >= interval)
  {
    previousmilliseconds = milliseconds;

    const float powerThreshold = 0.5; // Define a threshold for power change
    static float previousPower = -1.0; // Store the previous power measurement
    float current = ina226.getCurrent(); // Get current in Amperes
    float power = ina226.getPower(); // Get power in Watts);
    float busVoltage = ina226.getBusVoltage(); // Get current in Amperes
    float shuntVoltage = ina226.getShuntVoltage(); // Get current in Amperes
    std::string measurementDate = CurrentTime();

    //printINA226Values(current, busVoltage, shuntVoltage, power, measurementDate);

    if(busVoltage < 1.0)
    {
      Serial.println("Set LED to Red (255, 0, 0)");
      rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
    }
    else
    {
      Serial.println("Set LED to Green (255, 0, 0)");
      rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
    }

    Serial.print("Power: ");
    Serial.println(power, 2);

    Serial.print("Previous power: ");
    Serial.println(previousPower, 2);

    if(std::abs(power - previousPower) > powerThreshold)
    {
      postDataToServer(current, busVoltage, shuntVoltage, power, measurementDate);
      previousPower = power;
    }
  }
}

void connectToWifi()
{
  // Connect to Wi-Fi network
  const char* ssid = "Magellan";
  const char* password = "Nort4Sta$";
  const char* ntpServer = "pool.ntp.org";

  Serial.print("Connecting to Wi-Fi");

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);

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

void postDataToServer(float current, float busVoltage, float shuntVoltage, float power, const std::string& measurementDate)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWifi();
  }

  const char* serverUrl = "http://192.168.50.17/amps/store.php";
  
  // Example: Send data via HTTP POST with JSON
  //String jsonData = "{\"temperature\":24.5,\"humidity\":60}";
  std::string jsonData = std::format("{}?current={}&bus_voltage={}&measurement_date={}&shunt_voltage={}&power={}",
    serverUrl, current, busVoltage, measurementDate, shuntVoltage, power);

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

void printINA226Values(float current, float busVoltage, float shuntVoltage, float power, const std::string& measurementDate)
{
  Serial.print("Current: ");
  Serial.println(current, 2);
  
  Serial.print("Power: ");
  Serial.println(power, 2);
  
  Serial.print("Bus Voltage: ");
  Serial.println(busVoltage, 2);

  Serial.print("Shunt Voltage: ");
  Serial.println(shuntVoltage, 10);

  Serial.print("Measurement date: ");
  Serial.println(measurementDate.c_str());
}

//WITH Flagged AS
//	( SELECT *, SUM(CASE WHEN power = 0 THEN 1 ELSE 0 END) OVER (ORDER BY id ROWS UNBOUNDED PRECEDING) AS zero_group FROM Amperage ),
//Islands AS
//	( SELECT *, SUM(CASE WHEN power = 0 THEN 0 ELSE 1 END) OVER (ORDER BY id ROWS UNBOUNDED PRECEDING) AS island_id FROM Flagged ) 
//SELECT * FROM Islands;

// WITH Flagged AS
//   ( SELECT *, SUM(CASE WHEN power = 0 THEN 1 ELSE 0 END) OVER (ORDER BY id ROWS UNBOUNDED PRECEDING) AS zero_group FROM Amperage ), 
// Islands AS
//   ( SELECT *, SUM(CASE WHEN power = 0 THEN 0 ELSE 1 END) OVER (ORDER BY id ROWS UNBOUNDED PRECEDING) AS island_id FROM Flagged )
// SELECT sum(power) AS power_used,
// AVG(CASE WHEN power = 0 THEN NULL ELSE bus_voltage END) AS average_voltage,
// MIN(measurement_date) AS start_time,
// MAX(measurement_date) AS end_time,
// zero_group
// FROM Islands
// GROUP BY zero_group;
