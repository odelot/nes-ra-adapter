# nes-esp-firmware

We chose the **ESP32 C3 Super Mini model**. In the project it:

- Provides Internet connectivity to the NES.
- Manages a TFT screen to display unlocked achievements.
- Controls a buzzer for sound effects.
- Stores Wi-Fi and RetroAchievements (RA) credentials in EEPROM.
- Allows configuration via smartphone, connecting to the ESP32's access point.
- Supports configuration reset by holding the reset button for 5 seconds during startup.
- Uses LittleFS to store:
  - A CRC32-to-MD5 hash table (for cartridge identification).
  - Game and achievement images.
- Communicates with the Raspberry Pi Pico via Serial0 to:
  - Retrieve the game CRC.
  - Send HTTP requests to RetroAchievements.
  - Receive and forward HTTP responses.
- Controls analog switches to open and close the bus between the NES and the cartridge.

---

## 🔧 How to Compile the Firmware for ESP32-C3 Super Mini  

This guide explains how to set up **Arduino IDE (version 2.3.4)** to compile and upload the firmware to an **ESP32-C3 Super Mini**.  

---

### 📌 Prerequisites  

1. **Download and install Arduino IDE (2.3.4)**  
   - Get it from: [https://www.arduino.cc/en/software](https://www.arduino.cc/en/software)  

2. **Install ESP32 board support in Arduino IDE**  
   - Open **Arduino IDE**  
   - Go to **Tools** → **Board** → **Board Manager**  
   - Search for **esp32** and install the version `v3.0.5` of **ESP32 by Espressif Systems**  
   - Once installed, select **ESP32C3 Dev Module** under **Tools** → **Board**  

3. **Install the required libraries**  

   The project uses the following libraries, which you can install via the **Library Manager** or manually from GitHub:  

   - **WiFiManager** (v2.0.17) → [https://github.com/tzapu/WiFiManager](https://github.com/tzapu/WiFiManager)  
   - **ArduinoJson** (v7.3.0) → [https://github.com/bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)  
   - **PNGdec** (v1.0.3) → [https://github.com/bitbank2/PNGdec](https://github.com/bitbank2/PNGdec)  
   - **TFT_eSPI** (v2.5.43) → [https://github.com/Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)

   **To install via Library Manager:**  
   - Open **Tools** → **Library Manager**  
   - Search for the library name and click **Install**  

4. **Install the Arduino LittleFS Upload Plugin**

   Visit the [plug-in site](https://github.com/earlephilhower/arduino-littlefs-upload) and follow its instructions. This is used to upload the contains of `data` folder (the CRC32<->MD5 map) to the LittleFS.

---

### ⚙️ TFT_eSPI Library 

Copy the files from `miscellaneus/TFT_eSPI_config` into the library folder (usually `/user/Documents/Arduino/libraries/TFT_eSPI`). It contains the user settings for the LCD we are using and also a bugfix for the version of the library we are using.

### ⚙️ Configuring Arduino IDE for ESP32-C3  

Before compiling, adjust the settings in Arduino IDE:  

1. **Go to** Tools → Board → **ESP32C3 Dev Module**  
2. Configure the following options:  

   - **USB CDC On Boot**: Enabled
   - **CPU Frequency**: 160MHz (WiFi)   
   - **Core Debug Level**: None  
   - **Erase All Flash Before Sketch Upload**: Disabled  
   - **Flash Size**: 4MB (32Mb)  
   - **Partition Scheme**: No OTA (2MB APP/2MB SPIFFS)   
   - **Upload Speed**: 921600  

---

### 🚀 Compiling and Uploading to ESP32-C3  

1. **Connect the ESP32-C3** to your PC via USB  
2. **Select the correct port** under **Tools** → **Port**  
3. **Compile the code** by clicking the ✔️ (Verify) button  
4. **Upload the firmware** to the ESP32 by clicking the 🔼 (Upload) button  
5. **Upload the data** using the Arduino LittleFS Upload plug-in. `[Ctrl]` + `[Shift]` + `[P]`, then "Upload LittleFS to Pico/ESP8266/ESP32".

If everything is set up correctly, the firmware will be flashed, and the ESP32-C3 will be ready to run the project!  

---

### ❓ Troubleshooting  

If you encounter issues:  
- **Check the IDE console for errors** and ensure all required libraries are correctly installed.  
- **Confirm the correct board and port** are selected in the Arduino IDE.  
- **Try using a different USB cable or port** if the ESP32 is not detected.  
- **Press the "Boot" button on the ESP32-C3 during upload** if flashing fails.  
- **If you couldn't upload the data folder using the plug-in** follow the Troubleshooting found on the [plug-in site](https://github.com/earlephilhower/arduino-littlefs-upload). 

