#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define PIN_RX 32
#define PIN_TX 33

#define PIN_SD_CS 13
#define PIN_SD_MOSI 15
#define PIN_SD_SCK 14
#define PIN_SD_MISO 2

HardwareSerial hSerial(1);

bool isFileOpened = false;
bool sdInitialized = false;

void setup()
{
    Serial.begin(115200);

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

	hSerial.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
}

File file;

uint32_t lastByteReceived;

#define BUFFER_SIZE 255

void loop()
{
    //No data in 1 second, close the file
    if (file && isFileOpened && lastByteReceived + 1000 < millis()) {
        file.close();
        isFileOpened = false;
    }

    byte buffer[BUFFER_SIZE];

    byte dataLength = hSerial.read(buffer, sizeof(buffer));

    if (dataLength > 0) {

        lastByteReceived = millis();

        if (!isFileOpened) {
            file = SD.open("/LOG001.TXT", FILE_WRITE);
            isFileOpened = true;
        }

        if (!file) {
            Serial.println("error opening file");
        } else {
            file.write(buffer, dataLength);
        }

    }

}
