#include "waveshare_sd_card.h"

void listFilesRecursive(fs::FS &fs, const char * dirname, uint8_t levels);

esp_expander::CH422G *expander = NULL;

// Initial Setup
void setup(){
    Serial.begin(115200);


    delay(2000);
    Serial.println("Initialize IO expander");
    /* Initialize IO expander */
    expander = new esp_expander::CH422G(EXAMPLE_I2C_SCL_PIN, EXAMPLE_I2C_SDA_PIN, EXAMPLE_I2C_ADDR);
    expander->init();
    expander->begin();

    Serial.println("Set the IO0-7 pin to output mode.");
    expander->enableAllIO_Output();
    expander->digitalWrite(TP_RST , HIGH);
    expander->digitalWrite(LCD_RST , HIGH);
    expander->digitalWrite(LCD_BL , HIGH);

    // Use extended GPIO for SD card
    expander->digitalWrite(SD_CS, LOW);

    // Turn off backlight
    expander->digitalWrite(LCD_BL, LOW);

    // When USB_SEL is HIGH, it enables FSUSB42UMX chip and gpio19, gpio20 wired CAN_TX CAN_RX, and then don't use USB Function 
    expander->digitalWrite(USB_SEL, LOW);

    // Initialize SPI
    SPI.setHwCs(false);
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_SS);
    if (!SD.begin(SD_SS)) {
        Serial.println("Card Mount Failed"); // SD card mounting failed
        return;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached"); // No SD card connected
        return;
    }

    Serial.print("SD Card Type: "); // SD card type
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN"); // Unknown Type
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize); // SD card size

    Serial.println("Recursive listing of all files:");
    //listFilesRecursive(SD, "/", 10); // You can change depth (e.g., 10) as needed

}

// Main Loop
void loop() {
    
}


void listFilesRecursive(fs::FS &fs, const char * dirname, uint8_t levels) {
    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("DIR : ");
            Serial.println(file.path());
            if (levels) {
                listFilesRecursive(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("FILE: ");
            Serial.print(file.path());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}
