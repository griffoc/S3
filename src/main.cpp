#include <Arduino.h>
#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <ArduinoOTA.h>
#include <esp_pthread.h>
#include <HTTPClient.h>
#include <INA226.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>

#define SDA_PIN 18
#define SCL_PIN 46
#define USE_OTA // Uncomment to enable OTA updates
// #define TEST_WITHOUT_INA226
// #define TEST_WITHOUT_UPLOAD

const char* DataFileName = "data.txt"; // Path to the data file on LittleFS
const char* DataFilePath = "/data.txt"; // Path to the data file on LittleFS
const char* tempDirectory = "/temp"; // Path to the temporary directory on LittleFS

INA226 ina226(0x40); // Create an instance of the INA226 class
std::mutex serialPrintMutex;

void readCredentials(std::string&ssid, std::string& password);
bool connectToWifi();
std::string CurrentTime();
std::string generateTimeBasedFilename(const std::string& directory);
void initOTA();
void printSystemTime();
bool postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate);
bool writeToFile(float current, float busVoltage, float power, const std::string& measurementDate);
void uploadFileToServer();
void increaseStackSizeForUploadThread();
void serialPrint(const char* message, bool newLine = true);
void serialPrint(int message, bool newLine = true);

void setup()
{  
  Serial.begin(115200);
  increaseStackSizeForUploadThread();
  connectToWifi();
  LittleFS.begin(true); // Format on fail
  serialPrint("LittleFS initialized successfully");

#ifdef USE_OTA
  initOTA();
#endif

  uploadFileToServer();

  // Initialize I2C with the specified ESP32-S3 pins
  Wire.begin(SDA_PIN, SCL_PIN);
  
#ifndef TEST_WITHOUT_INA226
  //Initialize sensor module
  if(!ina226.begin())
  {
    serialPrint("INA226 not connected!");
    while(1)
      delay(1000);
  }

  ina226.setMaxCurrentShunt(20.0, 0.00375); // Set max current and shunt resistor value
#endif
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

#ifndef TEST_WITHOUT_INA226
    float current = ina226.getCurrent(); // Get current in Amperes
    float power = ina226.getPower(); // Get power in Watts);
    float busVoltage = ina226.getBusVoltage(); // Get current in Amperes
#else
    float current = 1.0;
    float power =  1.0;
    float busVoltage =  1.0;
#endif

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

    static bool postToServerFailed = false;
#ifndef TEST_WITHOUT_UPLOAD
    if(!postDataToServer(current, busVoltage, power, measurementDate))
#else
    static int count = 0;
    if(++count < 10)
#endif
    {
      writeToFile(current, busVoltage, power, measurementDate);
      postToServerFailed = true;
    }
    
    if(postToServerFailed &&
      connectToWifi())
    {
#ifdef TEST_WITHOUT_UPLOAD
    if(count >= 10)
    {
#endif
      uploadFileToServer();
      postToServerFailed = false;
#ifdef TEST_WITHOUT_UPLOAD
      count = 0;
    }
#endif
    }
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

  serialPrint("Stored SSID: ", false);
  serialPrint(ssid.c_str());
  
  // Always close the preferences when done
  preferences.end();
}

bool connectToWifi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return true;
  }

  const char* ntpServer = "pool.ntp.org";

  std::string ssid;
  std::string password;
  readCredentials(ssid, password);

  serialPrint("Connecting to Wi-Fi", false);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const int maxRetries = 20; // Maximum number of retries
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    serialPrint(".", false);
    retryCount++;
    if (retryCount >= maxRetries)
    {
      serialPrint("Failed to connect to Wi-Fi!");
      return false;
    }
  }

  // Set timezone and NTP server
  // Timezone format: TZ_OFFSET;DST_OFFSET,DST_START,DST_END
  configTime(0, 0, ntpServer); // 0 offset for UTC, adjust for local TZ

  serialPrint(".Connected to Wi-Fi!");
  serialPrint("IP Address: ", false);
  serialPrint(WiFi.localIP().toString().c_str());
  printSystemTime(); // Print the current local time

  return true;
}

void initOTA()
{
  // Configure ArduinoOTA
  ArduinoOTA.setPort(3232); // Default port
  ArduinoOTA.setHostname("esp32s3-ota"); // Target network name
  ArduinoOTA.setPassword("pa44word123"); // Optional authentication

  ArduinoOTA.onStart([]() { serialPrint("Start OTA Update"); });
  ArduinoOTA.onEnd([]() { serialPrint("\nEnd OTA Update"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error){
    Serial.printf("Error[%u]: ", error);
  });

  ArduinoOTA.begin();
}

bool postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate)
{
  if(!connectToWifi())
  {
    serialPrint("Failed to connect to Wi-Fi!");
    return false;
  }

  const char* serverUrl = "http://192.168.50.17/amps/store.php";
  
  // Example: Send data via HTTP POST with JSON
  std::string jsonData = std::format("{}?power={}&current={}&bus_voltage={}&measurement_date={}",
    serverUrl, power, current, busVoltage, measurementDate);

  HTTPClient http;
  http.begin(jsonData.c_str()); // Specify the URL
  
  serialPrint(jsonData.c_str());
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode <= 0) {
    serialPrint("Error on sending POST: ", false);
    serialPrint(httpResponseCode);
    return false;
  }
  
  http.end();

  return true;
}

bool writeToFile(float current, float busVoltage, float power, const std::string& measurementDate)
{
  //Check if file exists before opening it
  bool isExistingFile = LittleFS.exists(DataFilePath);

  File file = LittleFS.open(DataFilePath, "a");

  if (!file) 
  {
    serialPrint("Failed to create/open file for writing");
    return false;
  }

  if(!isExistingFile) // If the file is new write the header
  {
    file.println("power,current,busVoltage,measurementDate");
  }

  std::string dataLine = std::format("{:.2f},{:.2f},{:.2f},{}\n", current, busVoltage, power, measurementDate);
  file.print(dataLine.c_str());
  file.close();

  return true;
}

void uploadFileToServer()
{
  serialPrint("Attempting to upload file to server...");
  static std::atomic_bool uploadInProgress{false};
  bool filesReadyToUpload = false;

  if(uploadInProgress)
  {
    serialPrint("Upload already processing.");
    return;
  }

  bool dataFileExists = LittleFS.exists(DataFilePath);
  if(dataFileExists)
  {
    LittleFS.mkdir(tempDirectory); // Create a temporary directory to hold the file during upload

    std::string tempFilename = generateTimeBasedFilename(tempDirectory);
    while(LittleFS.exists(tempFilename.c_str())) // Ensure unique filename
    {
      tempFilename = generateTimeBasedFilename(tempDirectory);
      std::this_thread::yield(); // Yield to allow other tasks to run
    }

    LittleFS.rename(DataFilePath, tempFilename.c_str()); // Rename the file to avoid conflicts during upload
    
    filesReadyToUpload = true;
  }
  else
  {
    File root = LittleFS.open(tempDirectory);

    if(root && root.isDirectory())
    {
      File file = root.openNextFile();
      filesReadyToUpload = file ? true : false;
      file.close();
      root.close();
    }
  }

  if(filesReadyToUpload)
  {
    uploadInProgress = true;

    serialPrint("Starting upload thread...");

    std::jthread([]()
    {
      const size_t readBufSize = 0x7FF;
      char buffer[readBufSize] = {0};
      std::vector<std::string> filesToDelete; // Store the names of files to delete after successful upload

      File root = LittleFS.open(tempDirectory);

      if(!root || !root.isDirectory())
      {
        serialPrint("No files to upload.");
        uploadInProgress = false;
        return;
      }

      try
      {
        File file;
        while(file = root.openNextFile())
        {
          serialPrint(std::format("Uploading file: {}", file.path()).c_str());

          HTTPClient http;
          http.begin("http://192.168.50.17/amps/upload.php");
          
          http.addHeader("Content-Type", "application/octet-stream");

          // Send POST request using the Stream overload and exact content length size
          int httpResponseCode = http.sendRequest("POST", &file, file.size());

          if (httpResponseCode > 0)
          {
            filesToDelete.emplace_back(file.path()); // Mark the file for deletion after successful upload
          }
          else
          {
            serialPrint("Upload error: ", false);
            serialPrint(httpResponseCode);
            String response = http.getString();
            serialPrint(response.c_str());
          }

          file.close();

          http.end();
        }
      }
      catch(const std::exception& e)
      {
        serialPrint("Uploaded to server failed: ", false);
        serialPrint(e.what());
      }
      catch(...)
      {
        serialPrint("Uploaded to server failed: ", false);
        serialPrint("Unknown error");
      }

      root.close();

      serialPrint("Upload thread finished. Cleaning up files...");

      try
      {
        for(const auto& filename : filesToDelete)
        {
          serialPrint(std::format("Deleting file: {}", filename).c_str());
          if(!LittleFS.remove(filename.c_str()))
          {
            if(LittleFS.exists(filename.c_str()))
            {
              // WTF! throw?
              serialPrint("File still exists after deletion attempt");
            }
          }
        }
      }
      catch(const std::exception& e)
      {
        serialPrint("Error occurred while deleting files: ", false);
        serialPrint(e.what());
      }
      catch(...)
      {
        serialPrint("Unknown error occurred while deleting files");
      }

      uploadInProgress = false;

      serialPrint("Upload thread finished.");
    }).detach();
  }
  else
    serialPrint("No files to upload.");

}

void increaseStackSizeForUploadThread()
{
  static std::atomic_bool beenHere = false;

  if(beenHere)
    return;
  beenHere = true;

  auto cfg = esp_pthread_get_default_config();
  
  // Increase stack size (e.g., 4KB or 8KB depending on your needs)
  cfg.stack_size *= 4;
  cfg.thread_name = "upload_thread"; // Optional: helps with debugging
  //Apply config (applies ONLY to the next thread created on this core)
  esp_pthread_set_cfg(&cfg);
}

void printSystemTime()
{
  //WTF: getLocalTime must be called so that CurrentTime(), i.e chrono::system_clock::now(), returns
  //the correct time on the first call. Otherwise, 1970-01-01 00:00:00 is returned on the first few calls.
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    serialPrint("Failed to obtain time");
    return;
  }

  serialPrint("SystemTime: ", false);
  //serialPrint(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  serialPrint(CurrentTime().c_str());
}

std::string CurrentTime()
{
  return std::format("{:%Y-%m-%d%%20%T}", std::chrono::system_clock::now());
}

std::string generateTimeBasedFilename(const std::string& directory)
{
    return std::format("{}/{:%Y%m%d_%H%M%S}.txt", directory, std::chrono::system_clock::now());
}

void serialPrint(const char* message, bool newLine /* = true*/)
{
  std::lock_guard<std::mutex> lock(serialPrintMutex);
  if(newLine)
    Serial.println(message);
  else
    Serial.print(message);
}

void serialPrint(int message, bool newLine /* = true*/)
{
  std::lock_guard<std::mutex> lock(serialPrintMutex);
  if(newLine)
    Serial.println(message);
  else
    Serial.print(message);
}
