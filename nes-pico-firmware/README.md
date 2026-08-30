# nes-pico-firmware

In this project, the Raspberry Pi Pico has two Main Responsibilities:

1. Game Identification: Reads a portion of the cartridge to calculate the CRC and identify the game.

2. Memory Monitoring: Monitors the bus for memory writes and processes them using the rcheevos library.

This is distributed between cores in the following:

- Core 0:
  - Handles CRC calculation.
  - Executes rcheevos routines.
  - Manages serial communication with the ESP32 (primarily for Internet connectivity).

- Core 1:
  - Monitors the bus using three PIO state machines (no DMA):
    - **SM0** captures exactly one bus snapshot per CPU write cycle (sampled inside the data-valid window of M2 high) and keeps static mirrors of the NES RAM and cartridge SRAM up to date.
    - **SM1** watches for reads of the NMI vector ($FFFA/$FFFB) — the exact start of vblank — when core 1 takes an atomic snapshot of the mirrors and signals core 0 to run a rcheevos frame.
    - **SM2** watches for reads of the RESET vector ($FFFC/$FFFD) to detect a console reset and reset the rcheevos runtime state.
  - Writes to $4014 (OAM DMA) are kept as a vblank fallback for games that run with NMI disabled, plus a timer-based fallback for forced-blank periods.

*Note:* During game load the serial buffer holds up to ~100KB for the achievement list response; it is shrunk to 16KB once the game starts, freeing RAM for the rcheevos runtime.

---

## 🔧 How to Compile the Project for Raspberry Pi Pico Using VS Code  

This guide explains how to set up and compile the firmware for the **Raspberry Pi Pico** using **VS Code** with **Pico SDK 1.5.1**. The project integrates the **[rcheevos](https://github.com/RetroAchievements/rcheevos)** library via **CMake**, which is automatically downloaded and configured during compilation.  

---

### 📌 Prerequisites  

- **Hardware:** Raspberry Pi Pico Purple Board
- **Software:**  
  - **VS Code** (with recommended extensions such as **C/C++** and **CMake Tools**)  
  - **Pico SDK 1.5.1** installed and configured  
- **Environment Setup:**  
  - Ensure the **`PICO_SDK_PATH`** environment variable is correctly set to point to the Pico SDK directory.  
- **Reference Guide:**  
  - Follow this tutorial: [Raspberry Pi Pico and RP2040 – Part 1: Blink and VS Code](https://www.digikey.com.br/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-1-blink-and-vs-code/7102fb8bca95452e9df6150f39ae8422).  

---

### 🛠 Development Environment Setup  

1. **Clone the project repository** to your local machine
2. **Open the project in VS Code.**
3. **Ensure the development environment is properly configured:**
   - Confirm that PICO_SDK_PATH is correctly set in your system.
   - Install the necessary VS Code extensions (C/C++ and CMake Tools).

---

### ⚙️ CMake Configuration

- The project uses CMake as its build system.
- The rcheevos library is already configured in CMakeLists.txt and will be **automatically downloaded** during configuration and compilation.
- Check the `CMakeLists.txt` file to understand how the library and other dependencies are integrated.

---

### 🔨 Compiling the Project

1. Configure the Project:
   - Open the Command Palette in VS Code (Ctrl+Shift+P) and select CMake: Configure to initialize the project.

2. Build the Project:
   - Once configured, use CMake: Build to compile the firmware.
   - If everything is set up correctly, the build process will generate a .uf2 file in the output directory.

---

### 🚀 Flashing the Firmware to the Raspberry Pi Pico

1. Connect the Raspberry Pi Pico:
   - Plug the Pico into your computer while holding down the BOOTSEL button.

2. Copy the UF2 File:
   - The Pico will appear as a USB storage device. Simply drag and drop the .uf2 file onto this drive to flash the firmware.
