# NES RA Adapter Project
[![pt-br](https://img.shields.io/badge/lang-pt--br-green.svg)](https://github.com/odelot/nes-ra-adapter/blob/main/README.pt-br.md)

Repository of the **NES RetroAchievements Adapter** project – a maker initiative with a "cyberpunk western" style that transforms the physical NES console (released in 1985) by adding Internet connectivity and achievement functionality, supported by the amazing RetroAchievements community.

<p align="center">
  <a href="https://youtu.be/u1GWOFgOU88">
    <img width="70%" src="https://github.com/odelot/nes-ra-adapter/blob/main/images/video.png">
  </a>
</p>

-> -> -> **One month duration (April) Donation campaign to help the project ([campaign page](https://github.com/odelot/nes-ra-adapter-donation-campaign))** <- <- <-

---

## Table of Contents

- [Disclaimer](#disclaimer)
- [Introduction](#introduction)
- [Getting Started (How to Build)](#getting-started--how-to-build)
- [Architecture and Functionality](#architecture-and-functionality)
- [Limitations](#limitations)
- [General Information](#general-information)
- [Next Steps](#next-steps)
- [Version History](#version-history)
- [Contributions](#contributions)
- [License](#license)
- [Acknowledgments](#acknowledgments)
- [Similar Projects](#similar-projects)

---

## Disclaimer

The project is not responsible for any use or damage it may cause. Build and use it at your own risk. This is a **PROTOTYPE** that works but still needs to evolve into a final version—with revised hardware, extensive testing, and easy replication. We want everyone to be able to build and use this prototype, but only attempt it if you know what you're doing.

---

## Introduction

The **NES RA Adapter** transforms your original NES console into an interactive, Internet-connected machine, allowing real-time achievement unlocking through the RetroAchievements platform. Inspired by the Game Genie with a touch of cyberpunk western, the adapter:
- **Identifies the game** by reading the cartridge (via CRC calculation).
- **Monitors the console's memory** to detect events and achievements.
- **Provides connectivity and interface** through a TFT screen, buzzer, and smartphone configuration.

### Repository Organization

- **nes-pico-firmware**: source code for the firmware used on the Raspberry Pi Pico (Chinese purple board).
- **nes-esp-firmware**: source code for the firmware used on the ESP32 C3 Super Mini.
- **hardware**: schematics for building your version on a protoboard/perfboard and PCB files for prototype versions v0.1 and v0.2.
- **3d parts**: Fusion 360 and STL files for the modeled case, inspired by the Game Genie.
- **misc**ellaneous: auxiliary tool and files (CRC32<->RA Hash map creator / AWS Lambda to shrink RA response / TFT config).

---

## Getting Started / How to Build

### Acquire the Parts

| Part Name                        | Quantity   | Total Cost (USD)*    |
|----------------------------------|------------|----------------------|
| 72-pin slot**                    | 2x         | (2x ~$2.50) ~ $5.00  |
| SN74HC4066***                    | 7x         | (7x ~$0.85) ~ $6.00  |
| Raspberry Pi Pico (purple board) | 1x         | ~ $3.00              |
| ESP32 C3 Supermini               | 1x         | ~ $2.50              |
| LCD 240x240 driver ST7789        | 1x         | ~ $2.75              |
| Buzzer 3V + Transistor BC548     | 1x         | ~ $1.25              |
| **Total** (without PCB, 3D case) |            | ~ **$20.50**         |

\* Source: Aliexpress <br/>
\** In the miniaturized version, only 1 slot is needed <br/>
\*** A more stable alternative to SN74HC4066 would be 26x SN74LVC1G3157DBVR (SMD) - see details [here](https://github.com/odelot/nes-ra-adapter/tree/main/hardware#sn74hc4066-vs-sn74lvc1g3157dbvr).

### Assemble the Circuit

Currently, the project offers two options for circuit assembly:
1. **Protoboard/Perfboard:** Assemble the circuit using a protoboard, following the schematic available in the `hardware` folder.
2. **PCBs:** Order the PCBs (v0.1 or v0.2) using the files in the `hardware` folder for a more robust assembly.

### Update the Pico and ESP32 Firmware

1. Download the firmwares from the *Releases* section.
   
2. **Raspberry Pi Pico** - Connect the Pico to the computer via USB while holding the BOOTSEL button. A folder will open on your desktop. Copy the `nes-pico-firmware.uf2` file to this folder. This [guide](https://www.youtube.com/watch?v=os4mv_8jWfU) can help you understand the process.
   
3. **ESP32 C3 Supermini** – You'll need to install esptool. Here’s a [guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html#installation). If you're using the Arduino IDE with ESP32, you already have it installed.
   - First, identify the COM port assigned to the ESP32 when connected via USB (in this example, it's COM11).
   - Open the command line, navigate to the nes-esp-firmware folder inside the release files, and run the following commands (change the COM port approperly):

```
esptool.exe --chip esp32c3 --port "COM11" --baud 921600  --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x0 "nes-esp-firmware.ino.bootloader.bin" 0x8000 "nes-esp-firmware.ino.partitions.bin" 0xe000 "boot_app0.bin" 0x10000 "nes-esp-firmware.ino.bin" 

esptool.exe --chip esp32c3 --port COM11 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 2162688 ra_rash_map.bin
```

## Architecture and Functionality

The adapter uses two microcontrollers working together:

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/architecture.png"/>
</p>

### Raspberry Pi Pico

- **Game Identification:** Reads part of the cartridge to calculate the CRC and identify the game.
- **Memory Monitoring:** Uses two cores to monitor the bus and process writes using the rcheevos library:
  - **Core 0:** Calculates the CRC, runs rcheevos routines, and manages serial communication with the ESP32.
  - **Core 1:** Uses PIO to intercept the bus and detect stable values in write operations. Employs DMA to transfer data to a ping-pong buffer and forwards relevant addresses to Core 0 via a circular buffer.
- **Note:** Serial communication has a limit of approximately 32KB, restricting the size of the achievement list response.

### ESP32

- **Connectivity and Interface:** Provides Internet to the NES and manages a TFT screen to display achievements, along with a buzzer for sound effects.
- **Configuration Management:** Stores Wi-Fi and RetroAchievements credentials in EEPROM; configuration is done via smartphone connected to the ESP32 access point.
- **File System and Communication:** Uses LittleFS to store a hash table (CRC32 to MD5) for cartridge identification and game images.

---

## Limitations

- **Testing with different games:** The adapter has been tested with about 50 games, and issues were detected in 2. Check the [compatibility page](https://github.com/odelot/nes-ra-adapter/blob/main/Compatibility.md) for more details. We are working to improve compatibility.

- **Frame and reset detection:** It is not possible to detect a frame by simply inspecting cartridge signals. A heuristic is used, which has shown good results so far, but there is no guarantee it will work for all achievements. Additionally, console RESET detection is not yet implemented, requiring the console to be turned off and on for a reset.

- **Server Response Size:** RAM is limited to 32KB for storing the RetroAchievements response, which includes the list of achievements and memory addresses to monitor. The source-code of a AWS Lambda function is available (misc folder) to remove unnecessary fields or reduce the achievement list, ensuring the response fits within this limit.

---

## General Information

- **Power Consumption:** The adapter typically consumes 0.105A (max ~0.220A). The LCD screen consumes about 0.035A. Consumption is within the limit of the 7805 voltage regulator present in the NES

- **Logic Level Conversion (5V vs 3.3V):** The prototypes do not use logic level conversion to avoid signal delays. The bus between the cartridge and the NES is disabled when identifying the cartridge (not affecting the NES), and current levels are within Pico’s limits. In the final version, current limiting will be considered to reduce stress on the Pico. If someone in the community identifies the need for level shifting and develops a test scheme without compromising signals, contributions are welcome.

---

## Next Steps

### 1 - Miniaturize the Circuit:
Develop a board approximately the size of a Game Genie that fits into the NES 001 cartridge slot. Include a ground plane, decoupling capacitors, current-limiting resistors, and smaller traces to reduce interference.

### 2 - Adapt the Case to the Miniaturized Circuit:
Update the case (currently inspired by the Game Genie) to fit the new circuit layout.

### 3 - Optimize the Source Code:
Adjust image management in LittleFS (currently, ~270 images fit before space runs out), reduce memory fragmentation on ESP32, improve HTTP error handling (retries, timeouts), and enhance Pico-ESP32 communication and power usage.

---

## Version History

- **Version 0.1** – Initial prototype version.

---

## Contributions

Everyone is welcome to contribute!
- Submit improvements and pull requests via GitHub.
- Join our Discord channel: [https://discord.gg/baM7y3xbsA](https://discord.gg/baM7y3xbsA)
- Support us by [buying a coffee](https://buymeacoffee.com/nes.ra.adapter) to help with hardware prototype costs.

---

## License

- This project source code is distributed under the **GPLv3** license.
- The PCB circuits are distributed under **CC-BY-4.0** license.
- The 3d parts are distributed under **CC-BY-NC-4.0** license.

---

## Acknowledgments

- Thanks to **RetroAchievements** and its community for providing achievements for nearly all games and making the system available to everyone.
- Thanks to **NESDEV** for detailed NES documentation and a welcoming forum.

## Similar Projects

- **RA2SNES**: [https://github.com/Factor-64/RA2Snes](https://github.com/Factor-64/RA2Snes) - RA2Snes is a program built using Qt 6.7.3 in C++ and C that bridges the QUsb2Snes webserver & rcheevos client to allow unlocking Achievements on real Super Nintendo Hardware through the SD2Snes USB port.

