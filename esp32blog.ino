#include "Arduino.h"
#define FF_USE_FASTSEEK 1
#define SD_FREQ_KHZ 10000
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "FS.h"
#include "SD_MMC.h"
#include "FFat.h"
#include "DNSServer.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "RGB_lamp.h"
#include "time.h"
#include "HTTPClient.h"


// SD card pinout for Waveshare ESP32-S3 set for your device
#define SD_CLK_PIN 14
#define SD_CMD_PIN 15
#define SD_D0_PIN 16
#define SD_D1_PIN 18
#define SD_D2_PIN 17
#define SD_D3_PIN 21

//change these
#define ADMIN_USERNAME ""
#define ADMIN_PASSWORD ""
// uncomment if you want to use duckdns.org
//#define USEDUCK 
//your duckdns.org domain
#define DOMAIN ""
//your duckdns.org token
#define DDNSTOKEN ""
//uncomment if you want to use a sd card
#define USESD

Preferences eeprom;
HTTPClient http;

const char *ssid = "";
const char *password = "";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -25200;    // Example for PDT (GMT-7) in seconds
const int daylightOffset_sec = 3600;  // Example for Daylight Saving Time

unsigned long lastTempReading = 0;
unsigned long lastDomainUpdate = 0;
float currentTempC = 0.0;
//pinMode(2, OUTPUT);  //active fan control if installed
//digitalWrite(2,LOW); //off


uint32_t totalBytes;
float totalMB;

AsyncWebServer server(80);

String entriesFile = "/entries.json";
int ENTRIES_PER_PAGE = 5;

uint8_t currentLEDMode = 0;  // 0=off, 1=rainbow, 2=solid color
uint8_t solidR = 0, solidG = 0, solidB = 0;

void RGB_SetColor(uint8_t r, uint8_t g, uint8_t b) {
  solidR = r;
  solidG = g;
  solidB = b;
  currentLEDMode = 2;
  Set_Color(g, r, b);
}

void updateDomain() {
#ifdef USEDUCK
 // Specify the URL
  String url = "http://www.duckdns.org/update?domains=" + String(DOMAIN) + "&token=" + String(DDNSTOKEN);
  http.begin(url); 
  char httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.print("domain updated: ");
    Serial.println(payload);  // The fetched web page
  } else {
    Serial.print("domain Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
#endif
  lastDomainUpdate = millis();
}


void RGB_SetMode(uint8_t mode) {
  currentLEDMode = mode;
  if (mode == 0) {
    // Turn off LED immediately
    Set_Color(0, 0, 0);
  } else if (mode == 2) {
    Set_Color(solidG, solidR, solidB);
  }
}

void updatePageServed() {
  eeprom.begin("storage", false);                               //open the storage namespace for rw
  unsigned int pageServed = eeprom.getUInt("pageCounter", 50);  //get value for key, assign 0 on creation
  ++pageServed;
  eeprom.putUInt("pageCounter", pageServed);  //save pagenum to nvram
  eeprom.end();
}

unsigned int returnPageServed() {
  eeprom.begin("storage", false);                              //open the storage namespace for rw
  unsigned int pageServed = eeprom.getUInt("pageCounter", 0);  //get value for key, assign 0 on creation
  eeprom.end();
  return pageServed;
}


void handleUpload(AsyncWebServerRequest *request, String filename, size_t index,
                  uint8_t *data, size_t len, bool final) {
  static File uploadFile;   // must be static to persist across chunks
  static String targetPath; // store the actual file path being written

  // Stage 1: The start of the upload (index == 0)
  if (!index) {
    // Make sure filename starts with '/' so it’s valid
    if (!filename.startsWith("/")) {
      targetPath = "/" + filename;
    } else {
      targetPath = filename;
    }

#ifdef USESD
    if (SD_MMC.exists(targetPath)) {
      SD_MMC.rename(targetPath, targetPath + ".bak");
    }
    uploadFile = SD_MMC.open(targetPath, FILE_WRITE);
#else
    if (FFat.exists(targetPath)) {
      FFat.rename(targetPath, targetPath + ".bak");
    }
    uploadFile = FFat.open(targetPath, FILE_WRITE);
#endif

    Serial.printf("Starting upload of %s\n", targetPath.c_str());
  }

  // Stage 2: In-progress data chunks (len > 0)
  if (len && uploadFile) {
    uploadFile.write(data, len);
    Serial.printf("Writing %u bytes at index %u\n", len, index);
  }

  // Stage 3: The end of the upload (final == true)
  if (final && uploadFile) {
    uploadFile.close();
    Serial.printf("Upload finished: %s, size: %u\n", targetPath.c_str(), index + len);

    request->send(200, "text/plain", "File uploaded successfully!");
  }
}

void createEntriesFile() {
File filehndl;
#ifdef USESD  
  if (!SD_MMC.exists(entriesFile)) {
    Serial.println("Entries file not found. Generating default.");
    filehndl = SD_MMC.open(entriesFile, FILE_WRITE);
  }
#else 
  if (!FFat.exists(entriesFile)){
    Serial.println("Entries file not found. Generating default.");
    filehndl = FFat.open(entriesFile, FILE_WRITE);
  }
#endif

  if (!filehndl) {
      Serial.println("Entries file exists, skipping creation");
    } else {
      Serial.println("created entries file");
      filehndl.println("{");
      filehndl.println("\"entries\":[]");
      filehndl.println("}");
      filehndl.close();
    }
}

// helper: read JSON file into DynamicJsonDocument
bool loadEntries(DynamicJsonDocument &doc) {
  File file;  
#ifdef USESD  
  file = SD_MMC.open(entriesFile, FILE_READ);
#else
  file = FFat.open(entriesFile, FILE_READ);
#endif
  if (!file) return false;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error;
}

// helper: save JSON doc back to file
bool saveEntries(DynamicJsonDocument &doc) {
  File file;  
#ifdef USESD  
  file = SD_MMC.open(entriesFile, FILE_WRITE);
#else
  file = FFat.open(entriesFile, FILE_WRITE);
#endif
  
  if (!file) return false;
  serializeJson(doc, file);
  file.close();
  return true;
}

String WhatTimeIsIt() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char timeStringBuff[32];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  return timeStringBuff;
}


void setup() {
  Serial.begin(115200);
  btStop();


  // Initialize SD card
#ifdef USESD
   if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN)) {
    Serial.println("ERROR: SDMMC Pin configuration failed!");
    return;
   }
   if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 12)) {
        Serial.println("SD Card Mount Failed");
        return;
    }
    Serial.println("SD Card mounted");
#else
    if (!FFat.begin(true)) {
        Serial.println("Failed to mount internal FAT filesystem, formatting");
        while (1) delay(1000);
    }
    Serial.println("Internal FAT filesystem mounted");

    totalBytes = FFat.totalBytes();
    totalMB = (float)totalBytes / (1024.0 * 1024.0);
    Serial.print("File system size: ");
    Serial.print(totalMB, 2); // Print with 2 decimal places
    Serial.println(" MB");
#endif

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // serve public index.html

  // server.setSSL(server_cert, server_key);

  createEntriesFile();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    updatePageServed();
#ifdef USESD
    request->send(SD_MMC, "/index.html", "text/html");
#else
    request->send(FFat, "/index.html", "text/html");
#endif
  });

  // serve admin dashboard
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(ADMIN_USERNAME, ADMIN_PASSWORD)) {
      return request->requestAuthentication();
    }
#ifdef USESD
 if (!SD_MMC.exists("/admin.html")) {
    request->send(200,"text/html","<!DOCTYPE html><html lang=en><head><title>File Upload</title></head><body><h1>Upload File</h1><form enctype=multipart/form-data method=POST action=/import><input type=file name=file required><br><br><input type=text name=filename placeholder=Target_filename  value=Target_filename required><br><br><button type=submit>Upload</button></form></body></html>");
 } else request->send(SD_MMC, "/admin.html", "text/html");

#else
    if (!FFat.exists("/admin.html")) {
    request->send(200,"text/html","<!DOCTYPE html><html lang=en><head><title>File Upload</title></head><body><h1>Upload File</h1><form enctype=multipart/form-data method=POST action=/import><input type=file name=file required><br><br><input type=text name=filename placeholder=Target_filename  value=Target_filename required><br><br><button type=submit>Upload</button></form></body></html>");
 } else request->send(FFat, "/admin.html", "text/html");
#endif
  });

  server.on("/shutdown", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Server has been shut down");
    RGB_SetMode(0);
    Serial.println("Entering Deep Sleep");
    esp_deep_sleep_start();
  });

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(401, "text/plain", "You've been logged out");  // Sending 401 forces re-authentication
  });

  server.on("/count", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(8192);
    int total = 0;

    if (loadEntries(doc)) {
      JsonArray entries = doc["entries"].as<JsonArray>();
      total = entries.size();
    }

    DynamicJsonDocument out(256);
    out["total"] = total;

    String response;
    serializeJson(out, response);
    request->send(200, "application/json", response);
  });

  server.on("/pagecount", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument out(56);
    out["pagecount"] = returnPageServed();
    String response;
    serializeJson(out, response);
    request->send(200, "application/json", response);
  });

  server.on("/pagesize", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument out(56);
    out["pagesize"] = ENTRIES_PER_PAGE;
    String response;
    serializeJson(out, response);
    request->send(200, "application/json", response);
  });

  // form processing
  server.on("/forms", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("action", true)) {
      String action = request->getParam("action", true)->value();

      DynamicJsonDocument doc(8192);
      if (!loadEntries(doc)) {
        doc["entries"] = JsonArray();
      }

      if (action == "upload") {
        String title = request->getParam("title", true)->value();
        String content = request->getParam("content", true)->value();

        JsonArray entries = doc["entries"].as<JsonArray>();
        JsonObject newEntry = entries.createNestedObject();

        newEntry["title"] = title;
        newEntry["content"] = content;
        newEntry["timestamp"] = WhatTimeIsIt();  // assumes you have a timestamp function
        saveEntries(doc);
        request->send(200, "text/plain", "Entry uploaded");

      } else if (action == "delete") {
        String id = request->getParam("id", true)->value();
        JsonArray entries = doc["entries"].as<JsonArray>();
        for (int i = 0; i < entries.size(); i++) {
          if (entries[i]["timestamp"] == id) {
            entries.remove(i);
            break;
          }
        }
        saveEntries(doc);
        request->send(200, "text/plain", "Entry deleted");
      } else if (action == "edit") {
        String id = request->getParam("id", true)->value();
        String newContent = request->hasParam("content", true)
                              ? request->getParam("content", true)->value()
                              : "";
        String newTitle = request->hasParam("title", true)
                            ? request->getParam("title", true)->value()
                            : "";

        JsonArray entries = doc["entries"].as<JsonArray>();
        for (JsonObject entry : entries) {
          if (entry["timestamp"] == id) {
            if (newContent.length() > 0) entry["content"] = newContent;
            if (newTitle.length() > 0) entry["title"] = newTitle;
            break;
          }
        }
        saveEntries(doc);
        request->send(200, "text/plain", "Entry updated");
      }
    } else {
      request->send(400, "text/plain", "Missing action");
    }
  });

  // API to fetch entries

  server.on("/entries", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(8192);
    if (!loadEntries(doc)) {
      request->send(200, "application/json", "{\"entries\":[]}");
      return;
    }

    JsonArray entries = doc["entries"].as<JsonArray>();

    // Check if "all" parameter is present
    if (request->hasParam("all")) {
      String response;
      serializeJson(doc, response);  // returns { "entries": [ ... ] }
      request->send(200, "application/json", response);
      return;
    }

    // Default: paginated view
    int start = 0;
    if (request->hasParam("start")) {
      start = request->getParam("start")->value().toInt();
    }

    DynamicJsonDocument out(8192);
    JsonArray slice = out.createNestedArray("entries");

    for (int i = start; i < start + ENTRIES_PER_PAGE && i < entries.size(); i++) {
      slice.add(entries[i]);
    }

    String response;
    serializeJson(out, response);
    request->send(200, "application/json", response);
  });

  // Import entries via JSON upload
  server.on(
    "/import", HTTP_POST, [](AsyncWebServerRequest *request) {
      // This is called after the file upload is finished
      Serial.println("upload finished");
      request->send(200);
    },
    handleUpload);

#ifdef USESD
  if (SD_MMC.exists("/preview.png")) {
    Serial.println("preview image enabled");
    server.serveStatic("/preview.png", SD_MMC, "/preview.png");
  }
#else
  if (FFat.exists("/preview.png")) {
    Serial.println("preview image enabled");
    server.serveStatic("/preview.png", FFat, "/preview.png");
  }
#endif

  updateDomain();
  server.begin();

}


void loop() {

  if (millis() - lastTempReading > 6000) {
    currentTempC = temperatureRead();
    RGB_SetMode(2);
    if (currentTempC > 62.0) {
      RGB_SetColor(255, 0, 0);
    } else if (currentTempC > 59.0) {
      RGB_SetColor(255, 128, 0);
      //     digitalWrite(2,LOW);
      //       Serial.println("fan on");
    } else if (currentTempC > 54.0) {
      RGB_SetColor(255, 255, 0);
      //      digitalWrite(2,HIGH);

    } else {
      RGB_SetColor(0, 255, 0);
    }
    lastTempReading = millis();
  }

  if (millis() - lastDomainUpdate > 3,600,000,000 ) {
    //once an hour
   updateDomain();
  }
}
