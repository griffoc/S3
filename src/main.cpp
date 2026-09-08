#include <Arduino.h>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <format>
#include <future>
#include <iostream>
#include <queue>
#include <random>
#include <regex>
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
//#define TEST_WITHOUT_INA226

using namespace std::chrono_literals;

const char* DataFileName = "data.txt"; // Path to the data file on LittleFS
const char* DataFilePath = "/data.txt"; // Path to the data file on LittleFS
const char* tempDirectory = "/temp"; // Path to the temporary directory on LittleFS

#ifndef TEST_WITHOUT_INA226
INA226 ina226(0x40); // Create an instance of the INA226 class
#endif

const int MaxRecordsPerUploadFile = 300; // Maximum number of records per file
std::condition_variable uploadCondition;
std::mutex filesToUploadMutex;

std::queue<std::string> errorQueue;
std::mutex errorQueueMutex;

void readCredentials(std::string&ssid, std::string& password);
bool connectToWifi(bool fromSetup = false);
std::string CurrentTime();
std::string generateTimeBasedFilename(const std::string& directory);
void initOTA();
void printSystemTime();
bool writeToFile(float power, float current, float busVoltage, const std::string& measurementDate);
void uploadFileToServer();
void increaseStackSizeForUploadThread();
void serialPrint(const char* message, bool newLine = true);
void serialPrint(int message, bool newLine = true);

void setup()
{  
  Serial.begin(921600);
  increaseStackSizeForUploadThread();
  connectToWifi(true);
  LittleFS.begin(true); // Format on fail
  serialPrint("LittleFS initialized successfully");

#ifdef USE_OTA
  initOTA();
#endif

  // Initialize I2C with the specified ESP32-S3 pins
  Wire.begin(SDA_PIN, SCL_PIN);
  
#ifndef TEST_WITHOUT_INA226
  //Initialize sensor module
  if(!ina226.begin())
  {
    serialPrint("INA226 not connected!");
    while(true)
      delay(1000);
  }

  ina226.setMaxCurrentShunt(20.0, 0.00375); // Set max current and shunt resistor value
#endif

  std::jthread(uploadFileToServer).detach(); // Start the upload thread
  uploadCondition.notify_one();
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

    {
      std::lock_guard<std::mutex> lock(errorQueueMutex);
      while(!errorQueue.empty())
      {
        serialPrint(errorQueue.front().c_str());
        errorQueue.pop();
      }
    }

    rgbLedWrite(RGB_BUILTIN, 0, 255, 0);  //Assume all good green LED

#ifndef TEST_WITHOUT_INA226
    if(!ina226.isConnected())
    {
      serialPrint("INA226 not connected!");
      rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
      return; // Exit the loop if INA226 is not connected
    }

    float current = ina226.getCurrent(); // Get current in Amperes
    float power = ina226.getPower(); // Get power in Watts);
    float busVoltage = ina226.getBusVoltage(); // Get current in Amperes
#else
    float current = 1.0;
    float power =  1.0;
    float busVoltage =  1.0;
#endif

    if(current < 0.0 || power < 0.0)
    {
      current = 0.0; // Ensure current is not negative
      power = 0.0; // Ensure power is not negative
    }

    std::string measurementDate = CurrentTime();

    writeToFile(power, current, busVoltage, measurementDate);
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

bool connectToWifi(bool fromSetup /*= false*/)
{
  try
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
      return true;
    }

    std::string ssid;
    std::string password;
    readCredentials(ssid, password);

    serialPrint("Connecting to Wi-Fi", false);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    const int maxRetries = 4; // Maximum number of retries
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      rgbLedWrite(RGB_BUILTIN, 255, 255, 0);
      delay(1000);
      rgbLedWrite(RGB_BUILTIN, 0, 0, 0);

      serialPrint(".", false);

      if(fromSetup)
        continue; //Continue indefinitely if called from setup()
      else if(++retryCount <= maxRetries)
      {
        serialPrint("Failed to connect to Wi-Fi!");
        rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
        return false;
      }
    }

    serialPrint(".Connected to Wi-Fi!");
    serialPrint("IP Address: ", false);
    serialPrint(WiFi.localIP().toString().c_str());

    if(fromSetup)
    {
      const char* ntpServer = "pool.ntp.org";

      // Set timezone and NTP server
      // Timezone format: TZ_OFFSET;DST_OFFSET,DST_START,DST_END
      configTime(0, 0, ntpServer); // 0 offset for UTC, adjust for local TZ

      printSystemTime(); // Print the current local time
    }

    rgbLedWrite(RGB_BUILTIN, 0, 255, 0);

    return true;
  }
  catch(const std::exception& e)
  {
    std::lock_guard<std::mutex> lock(errorQueueMutex);
    errorQueue.push(std::format("Wi-Fi connection failed: {}", e.what()));
    rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
  }
  catch(...)
  {
    std::lock_guard<std::mutex> lock(errorQueueMutex);
    errorQueue.push("Wi-Fi connection failed: Unknown error");
    rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
  }

  return false;
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

bool writeToFile( float power, float current, float busVoltage, const std::string& measurementDate)
{
  static u32_t recordCount = 0;
  const std::string dataLine = std::format("{:.2f},{:.2f},{:.2f},{}\n", power, current, busVoltage, measurementDate);

  if(++recordCount >= MaxRecordsPerUploadFile)
  {
    LittleFS.mkdir(tempDirectory); // Create a temporary directory to hold the file during upload

    std::string tempFilename = generateTimeBasedFilename(tempDirectory);

    {
      std::lock_guard<std::mutex> lock(filesToUploadMutex);
      LittleFS.rename(DataFilePath, tempFilename.c_str()); // Rename the file to avoid conflicts during upload
    }
  
    uploadCondition.notify_one(); // Notify the upload thread to upload files to server
    recordCount = 0; // Reset the record count for the new file
  }

  const bool isExistingFile = LittleFS.exists(DataFilePath);
  File file = LittleFS.open(DataFilePath, "a");

  if (!file) 
  {
    serialPrint("Failed to create/open file for writing");
    return false;
  }
  else if(recordCount == 0 && isExistingFile)
  {
    size_t fileSize = file.size();
    recordCount = fileSize / dataLine.length(); // Estimate record count
  }

  if(!isExistingFile) // If the file is new write the header
  {
    file.println("power,current,busVoltage,measurementDate");
  }

  file.print(dataLine.c_str());
  file.close();

  return true;
}

void uploadFileToServer()
{
  while(true)
  {
    std::vector<std::string> filesToUpload;

    try
    {
      {
        std::unique_lock<std::mutex> lock(filesToUploadMutex);
        uploadCondition.wait(lock);

        File root = LittleFS.open(tempDirectory);

        if(root && root.isDirectory())
        {
          File file;
          while(file = root.openNextFile())
          {
            filesToUpload.emplace_back(file.path());
            file.close();
          }

          root.close();
        }
      }
    }
    catch(const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(errorQueueMutex);
      errorQueue.push(std::format("Failed gathering files for upload: {}", e.what()));
    }
    catch(...)
    {
      std::lock_guard<std::mutex> lock(errorQueueMutex);
      errorQueue.push("Failed gathering files for upload: Unknown error");
    }

    //if not connected to Wi-Fi, wait on uploadCondition above (i.e. another MaxRecordsPerUploadFile)
    if(!connectToWifi())
      continue;

    for(const auto& filename : filesToUpload)
    {
      try
      {
        File file = LittleFS.open(filename.c_str(), "r");

        if(!file)
        {
          std::lock_guard<std::mutex> lock(errorQueueMutex);
          errorQueue.push(std::format("Failed to open file for upload: {}", filename));
          continue;
        }

        HTTPClient http;
        http.begin("http://192.168.50.17/amps/upload.php");
        http.addHeader("Content-Type", "application/octet-stream");

        // Send POST request using the Stream overload and exact content length size
        int httpResponseCode = http.sendRequest("POST", &file, file.size());

        file.close();

        if (httpResponseCode == 200)
        {
          LittleFS.remove(filename.c_str());
          std::lock_guard<std::mutex> lock(errorQueueMutex);
          errorQueue.push((std::format("Removed file: {}: httpResponse:{}", filename, httpResponseCode)));
        }
        else
        {
          String response = http.getString();
          std::lock_guard<std::mutex> lock(errorQueueMutex);
          errorQueue.push((std::format("Upload error: {}\n{}", httpResponseCode, response.c_str())));
        }

        http.end();
      }
      catch(const std::exception& e)
      {
        std::lock_guard<std::mutex> lock(errorQueueMutex);
        errorQueue.push(std::format("Uploaded to server failed: {}", e.what()).c_str());
      }
      catch(...)
      {
        std::lock_guard<std::mutex> lock(errorQueueMutex);
        errorQueue.push("Uploaded to server failed: Unknown error");
      }
    }
  }
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
  return std::format("{:%Y-%m-%d %T}", std::chrono::system_clock::now());
}

std::string generateTimeBasedFilename(const std::string& directory)
{
    return std::format("{}/{:%Y%m%d_%H%M%S}.txt", directory, std::chrono::system_clock::now());
}

void serialPrint(const char* message, bool newLine /* = true*/)
{
  if(newLine)
    Serial.println(message);
  else
    Serial.print(message);
}

void serialPrint(int message, bool newLine /* = true*/)
{
  if(newLine)
    Serial.println(message);
  else
    Serial.print(message);
}
