#include <Arduino.h>
//SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#include <ArduinoOTA.h>
#include <chrono>
#include <format>
#include <HTTPClient.h>
#include <Preferences.h>
#include <atomic>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <esp_pthread.h>

#define SDA_PIN 18
#define SCL_PIN 46
//#define USE_OTA // Uncomment to enable OTA updates

const char* DataFileName = "data.txt"; // Path to the data file on LittleFS
const char* DataFilePath = "/data.txt"; // Path to the data file on LittleFS
const char* tempDirectory = "/temp"; // Path to the temporary directory on LittleFS

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
  LittleFS.begin(true); // Format on fail
  Serial.println("LittleFS initialized successfully");
  //uploadFileToServer();

#ifdef USE_OTA
  initOTA();
#endif

  // Initialize I2C with the specified ESP32-S3 pins
  Wire.begin(SDA_PIN, SCL_PIN);
}

void loop()
{
#ifdef USE_OTA
  ArduinoOTA.handle(); // Critical: Must run constantly to intercept network files
#endif
  static unsigned long previousmilliseconds = 0;
  const long interval = 10;

  unsigned long milliseconds = millis();

  if (milliseconds - previousmilliseconds >= interval)
  {
    previousmilliseconds = milliseconds;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dist(1.0, 10.0);

    float current = dist(gen);
    float power = dist(gen);
    float busVoltage = dist(gen);

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

    static int count = 0;
    if(++count < 5000) // Every 3 seconds, write to file
    {
      writeToFile(current, busVoltage, power, measurementDate);
    }
    else
    {
      uploadFileToServer();
      count = 0;
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
  return false;
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

bool postDataToServer(float current, float busVoltage, float power, const std::string& measurementDate)
{
  return false;
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

  //Serial.println("Data written to file successfully");
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

    std::string currentTime = CurrentTime();
    std::erase_if(currentTime, [](char x){ return x == '-' || x == ':' || x == ' ' || x == '.' || x == '%'; });

    std::string tempFilename = std::format("{}/data_{}.txt", tempDirectory, currentTime);
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

      Serial.println("Upload thread started...");

      try
      {
        File file;
        while(file = root.openNextFile())
        {
          Serial.println(std::format("Uploading file: {}", file.path()).c_str());

          const char* Boundary = "----WebKitFormBoundary4SA2FQldVsXi3jQ";

          // if(!connectToWifi())
          // {
          //   break; //Cannot upload file if not connected to Wi-Fi
          // }
          
          // Construct multipart body manually or use a library
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
          
          Serial.println("HTTP connection established");

          // Set headers for file upload
          std::string boundaryHeader = std::format("multipart/form-data; boundary={}", Boundary);
          http.addHeader("Content-Type", boundaryHeader.c_str());

          filesToDelete.emplace_back(file.path()); // Mark the file for deletion after successful upload

          file.close();

          http.end();
          Serial.println("HTTP connection closed");
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
            LittleFS.exists(filename.c_str()) ? Serial.println("File still exists after deletion attempt") : Serial.println("File does not exist");
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
  auto cfg = esp_pthread_get_default_config();
  
  // Increase stack size (e.g., 4KB or 8KB depending on your needs)
  cfg.stack_size *= 4;
  cfg.thread_name = "upload_thread"; // Optional: helps with debugging
  //Apply config (applies ONLY to the next thread created on this core)
  esp_pthread_set_cfg(&cfg);
}
