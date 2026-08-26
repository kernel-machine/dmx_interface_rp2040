# Pico DMX Interface — Enttec DMX USB Pro Emulator (Dual Universe)

## Motivation
Lighting control hardware can often be expensive or difficult to customize. The motivation behind this project is to create an affordable, reliable, and highly customizable Dual-Universe DMX interface using the ubiquitous and powerful Raspberry Pi Pico. By emulating the industry-standard **Enttec DMX USB Pro Mk2** protocol, this interface works out of the box with popular lighting control software like QLC+, without requiring any custom drivers. 

It also adds standalone functionality, an OLED display, and physical controls to manage your DMX setups directly from the device.

## Features
- **Enttec Pro Mk2 Emulation**: Native compatibility with QLC+ and other major lighting software.
- **Dual Universe Output**: Supports two full DMX universes via MAX485 transceivers.
- **OLED Menu System**: Integrated SSD1306 OLED display and a 4-button interface for on-device configuration.
- **Standalone CUE Engine**: Record and store up to 10 dual-universe CUEs directly to the Pico's flash memory.
- **Smart Disconnect Behavior**: Configurable fallback modes when USB connection is lost:
  - Blackout
  - Hold Last Value
  - Play a specific recorded CUE
- **DMX Crossfading**: Smooth transitions with configurable fade times when switching between live USB control and standalone CUEs.
- **Advanced Routing**: Flexible routing to map Universe 1 and 2 to physical Outputs A and B.
- **Persistent Settings**: All configuration preferences (Refresh rate, Routing, Fade time, Screensaver timeout) are saved to EEPROM.

## How to flash
This project is built using **PlatformIO** and the Arduino framework for the Raspberry Pi Pico (Earle Philhower core).

### Option 1: Direct Flash via PlatformIO (Recommended)
1. Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO IDE extension](https://platformio.org/).
2. Open this project folder in VSCode.
3. While holding the **BOOTSEL** button on your Raspberry Pi Pico, plug it into your computer via USB.
4. Once the Pico mounts as a mass storage device, click the **Upload** button (right arrow icon) in the bottom PlatformIO toolbar. PlatformIO will compile the code and flash it automatically.

### Option 2: Drag and Drop (UF2)
1. Build the project in PlatformIO by clicking the **Build** button (checkmark icon).
2. Once the build is successful, navigate to `.pio/build/pico/` inside the project folder to find the generated `firmware.uf2` file.
3. Hold the **BOOTSEL** button on your Pico and plug it into your computer via USB.
4. The Pico will appear as a USB drive named `RPI-RP2`.
5. Drag and drop the `firmware.uf2` file into the `RPI-RP2` drive. The Pico will automatically reboot and run the new firmware.
