#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define PIN_RX 32
#define PIN_TX 33

#define PIN_SD_CS 13
#define PIN_SD_MOSI 15
#define PIN_SD_SCK 14
#define PIN_SD_MISO 2

/*
 * ESP has total 1024 bytes of RAM used by all 3 UARTs as a buffer
 * Default is 256 bytes, but increasing the value makes much more sense
 */
#define RX_BUFFER_SIZE 512

HardwareSerial hSerial(1);

bool isFileOpened = false;
bool sdInitialized = false;

File file;

uint32_t lastByteReceived;
uint32_t nextCleanupMs;

#define BUFFER_SIZE 255

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

    nextFileName = findFileName();
    Serial.println("Next filename: " + nextFileName);

	hSerial.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    // Set the buffer size
    hSerial.setRxBufferSize(RX_BUFFER_SIZE);
    hSerial.flush();
}

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
            file.write(buffer, dataLength);
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
