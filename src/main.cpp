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
//#define USE_OTA // Uncomment to enable OTA updates

const char* DataFileName = "data.txt"; // Path to the data file on LittleFS
const char* DataFilePath = "/data.txt"; // Path to the data file on LittleFS
const char* tempDirectory = "/temp"; // Path to the temporary directory on LittleFS

INA226 ina226(0x40); // Create an instance of the INA226 class

void readCredentials(std::string&ssid, std::string& password);
bool connectToWifi();
std::string CurrentTime();
void initOTA();
void printSystemTime();
bool postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate);
bool writeToFile(float current, float busVoltage, float power, const std::string& measurementDate);
void uploadFileToServer();
void increaseStackSizeForUploadThread();

void setup()
{  
  Serial.begin(115200);
  increaseStackSizeForUploadThread();
  connectToWifi();
  LittleFS.begin(true); // Format on fail
  Serial.println("LittleFS initialized successfully");
  //uploadFileToServer();

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

    if(!postDataToServer(current, busVoltage, power, measurementDate))
    {
      Serial.println("Failed to post data to server!");
      writeToFile(current, busVoltage, power, measurementDate);
    }
    else
    {
      uploadFileToServer();
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

  Serial.print("Stored SSID: ");
  Serial.println(ssid.c_str());
  
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

  Serial.print("Connecting to Wi-Fi");

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const int maxRetries = 20; // Maximum number of retries
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    retryCount++;
    if (retryCount >= maxRetries)
    {
      Serial.println("Failed to connect to Wi-Fi!");
      return false;
    }
  }

  // Set timezone and NTP server
  // Timezone format: TZ_OFFSET;DST_OFFSET,DST_START,DST_END
  configTime(0, 0, ntpServer); // 0 offset for UTC, adjust for local TZ

  Serial.println(".Connected to Wi-Fi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  printSystemTime(); // Print the current local time

  return true;
}

std::string CurrentTime()
{
  return std::format("{:%Y-%m-%d%%20%T}", std::chrono::system_clock::now());
}

std::string generateTimeBasedFilename(const std::string& directory)
{
    return std::format("{}/{:%Y%m%d_%H%M%S_%OS}.txt", directory, std::chrono::system_clock::now());
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

bool postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate)
{
  if(!connectToWifi())
  {
    Serial.println("Failed to connect to Wi-Fi!");
    return false;
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

  return true;
}

bool writeToFile(float current, float busVoltage, float power, const std::string& measurementDate)
{
  //Check if file exists before opening it
  bool fileExists = LittleFS.exists(DataFilePath);

  File file = LittleFS.open(DataFilePath, "a");

  if (!file) 
  {
    Serial.println("Failed to create/open file for writing");
    return false;
  }

  if(!fileExists) // If the file is new write the header
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
  Serial.println("Attempting to upload file to server...");
  static std::atomic_bool uploadInProgress{false};
  bool filesReadyToUpload = false;

  if(uploadInProgress)
  {
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

    Serial.println("Starting upload thread...");

    std::jthread([]()
    {
      const size_t readBufSize = 0x7FF;
      char buffer[readBufSize] = {0};
      std::vector<std::string> filesToDelete; // Store the names of files to delete after successful upload

      File root = LittleFS.open(tempDirectory);

      if(!root || !root.isDirectory())
      {
        Serial.println("No files to upload.");
        uploadInProgress = false;
        return;
      }

      try
      {
        File file;
        while(file = root.openNextFile())
        {
          Serial.println(std::format("Uploading file: {}", file.path()).c_str());

          const char* Boundary = "----WebKitFormBoundary4SA2FQldVsXi3jQ";

          if(!connectToWifi())
          {
            break; //Cannot upload file if not connected to Wi-Fi
          }
          
          // Construct multipart body manually
          std::stringstream body;
          body << std::format("--{}\n", Boundary);
          body << std::format("Content-Disposition: form-data; name=\"file\"; filename=\"{}\"\n", DataFileName);
          body << "Content-Type: text/plain\n\n";
          
          bool endsWithBoundary = false;
          size_t totalBytesRead = 0;
          while (file.available())
          {
            if(totalBytesRead >= 0xFFFF) // If the body exceeds 64KB, append the boundary
            {
              body << Boundary;
              endsWithBoundary = true;
            }

            size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer), readBufSize);

            if(bytesRead > 0)
            {
              body.write(buffer, bytesRead);
              totalBytesRead += bytesRead;
              endsWithBoundary = false;
            }
          }
            
          Serial.println(std::format("Read {} bytes from file", totalBytesRead).c_str());

          if(!endsWithBoundary)
            body << Boundary;
          body << "--\n";

          HTTPClient http;
          http.begin("http://192.168.50.17/amps/upload.php");
          
          // Set headers for file upload
          std::string boundaryHeader = std::format("multipart/form-data; boundary={}", Boundary);
          http.addHeader("Content-Type", boundaryHeader.c_str());

          int httpResponseCode = http.POST(body.str().c_str());
          
          if (httpResponseCode > 0)
          {
            filesToDelete.emplace_back(file.path()); // Mark the file for deletion after successful upload
          }
          else
          {
            Serial.print("Upload error: ");
            Serial.println(httpResponseCode);
          }

          file.close();

          http.end();
        }
      }
      catch(const std::exception& e)
      {
        Serial.print("Uploaded to server failed: ");
        Serial.println(e.what());
      }
      catch(...)
      {
        Serial.print("Uploaded to server failed: ");
        Serial.println("Unknown error");
      }

      root.close();

      Serial.println("Upload thread finished. Cleaning up files...");

      try
      {
        for(const auto& filename : filesToDelete)
        {
          Serial.println(std::format("Deleting file: {}", filename).c_str());
          if(!LittleFS.remove(filename.c_str()))
          {
            if(LittleFS.exists(filename.c_str()))
            {
              // WTF! throw?
              Serial.println("File still exists after deletion attempt");
            }
          }
        }
      }
      catch(const std::exception& e)
      {
        Serial.print("Error occurred while deleting files: ");
        Serial.println(e.what());
      }
      catch(...)
      {
        Serial.println("Unknown error occurred while deleting files");
      }

      uploadInProgress = false;

      Serial.println("Upload thread finished.");
    }).detach();
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
