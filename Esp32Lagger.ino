#include "FS.h"
#include "SD.h"
#include "SPI.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define PIN_RX 32
#define PIN_TX 33

#define PIN_SD_CS 13
#define PIN_SD_MOSI 15
#define PIN_SD_SCK 14
#define PIN_SD_MISO 2

const char* ssid = "Esp32Lagger";
const char* password = "123456789";

/*
 * ESP has total 1024 bytes of RAM used by all 3 UARTs as a buffer
 * Default is 256 bytes, but increasing the value makes much more sense
 */
#define RX_BUFFER_SIZE 512
#define BUFFER_SIZE 512
#define MIN_WRITE_SIZE 16    // Do not write less than this

AsyncWebServer server(80);
HardwareSerial hSerial(1);

bool isFileOpened = false;
bool sdInitialized = false;

File file;

uint32_t lastByteReceived;
uint32_t nextCleanupMs;
uint32_t nextStatsMs;


String nextFileName;

void left_fill_zeros(char* dest, const char* str, int length)
{
    sprintf(dest, "%.*d%s", (int)(length-strlen(str)), 0, str);
}

String findFileName() {

    String name;
    char filledIndex[10];
    char notFilledIndex[10];

    for (int i = 1; i < 1024; i++) {
        sprintf(notFilledIndex, "%d", i);
        left_fill_zeros(filledIndex, notFilledIndex, 5);

        name = "/LOG" + String(filledIndex) + ".txt";

        if (!SD.exists(name)) {
            return name;
        }
    }
    
    //As a filsafe, return predefined name
    return "/LOG0001.txt";
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

String fileList() {
    String pageContent;
    pageContent = "<!DOCTYPE html><html>";
    pageContent += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    pageContent += "<title>ESP32 Lagger</title>";
    pageContent += "<link rel=\"icon\" href=\"data:,\">";
    pageContent += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto;}";
    pageContent += "h1 { text-align: center;}";
    pageContent += "ul li { font-size: 1.5em;}";
    pageContent += ".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}";
    pageContent += ".button-small { background-color: #0000ff; border: none; color: white; padding: 8px 20px; text-decoration: none; font-size: 15px; margin: 2px; cursor: pointer;}";
    pageContent += ".button2 {background-color: #ff0000;}";
    pageContent += "</style></head>";
    
    pageContent += "<body>";
    pageContent += "<h1>ESP32 Lagger</h1>";
    
    pageContent += "<ul>";

    File root = SD.open("/");
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            pageContent += "<li><a href=\"/download?file=" + String(file.name()) + "\">" + String(file.name()) + "</a></li>";
        }
        file = root.openNextFile();
    }
    root.close();

    pageContent += "</ul>";
    
    pageContent += "</body></html>";

    return pageContent;
}

void setup()
{
    Serial.begin(115200);

    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    /*
     * Server paths definitions
     */
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", fileList());
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        String filename = request->getParam("file")->value();
        request->send(SD, filename, "application/octet-stream", true);
    });

    server.onNotFound(notFound);
    server.begin();

    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin()) {
        Serial.println("SD card init failed!");
        sdInitialized = false;
    } else {
        Serial.println("SD card initialized");
        sdInitialized = true;
    }

    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    nextFileName = findFileName();
    Serial.println("Next filename: " + nextFileName);

	hSerial.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    // Set the buffer size
    hSerial.setRxBufferSize(RX_BUFFER_SIZE);
    hSerial.flush();
}

uint32_t sdCardTime;

void loop()
{ 

    //No data in 1 second, close the file
    if (file && isFileOpened && lastByteReceived + 1000 < millis()) {
        file.close();
        isFileOpened = false;
        Serial.println("File closed");
        nextFileName = findFileName();
        Serial.println("Next filename: " + nextFileName);
    }

    byte buffer[BUFFER_SIZE];

    uint32_t availabeBytes = hSerial.available();

    /*
     * Read data from serial port if there are at last MIN_WRITE_SIZE bytes
     * or every 500ms to get final portion of the log
     */
    if (availabeBytes >= MIN_WRITE_SIZE || lastByteReceived + 500 < millis()) {
        byte dataLength = hSerial.read(buffer, sizeof(buffer));

        if (dataLength > 0 && millis() > 500) {

            lastByteReceived = millis();

            if (!isFileOpened) {
                file = SD.open(nextFileName, FILE_WRITE);
                isFileOpened = true;

                nextCleanupMs = millis() + 2000; //Cleanup ever 2s
                Serial.println("File created " + String(millis()) + " " + String(dataLength));
            }

            if (!file) {
                Serial.println("error opening file");
            } else {

                uint32_t start = micros();

                file.write(buffer, dataLength);

                if (dataLength > 1) {
                    sdCardTime += (micros() - start); //Log how many microseconds it took to write to SD card

                    // Serial.println(String(dataLength) + " " + String(micros() - start));
                }
            }

            /*
            * From time to time close and reopen the file to synch all changes to the file system
            */
            if (file && millis() > nextCleanupMs) {

                file.flush();
                Serial.println("Cleanup");
                nextCleanupMs = millis() + 2000;
            }
        }
    }

    /*
     * Write SD card usage statistics
     */
    if (millis() > nextStatsMs) {
        nextStatsMs = millis() + 1000;
        Serial.println("SD card time: " + String((sdCardTime / 1000000.0f) * 100) + "%");
        sdCardTime = 0;
    }

}
