# DMX USB Interface 2 Universe (Enttec DMX USB Pro Emulator)
<img src="https://github.com/user-attachments/assets/eee21a7f-7b06-4964-9512-9229dd2f2e9b" 
  alt="DMX Node" 
  width="500" 
  style="display: block; margin: 0 auto" />

DIY DMX USB Interface with 2 Universe built around a Raspberry RP2040

## Motivation
Lighting control hardware can often be expensive or difficult to customize. The motivation behind this project is to create an affordable, reliable, and highly customizable Dual-Universe DMX interface using the ubiquitous and powerful Raspberry Pi Pico. By emulating the industry-standard **Enttec DMX USB Pro Mk2** protocol, this interface works out of the box with popular lighting control software like QLC+, without requiring any custom drivers. 

It also adds standalone functionality, an OLED display, and physical controls to manage your DMX setups directly from the device.

### Why USB and not ArtNet?
My first prototype used ArtNet, however for my use cases it was more convenient to connect the PC and the interface via a USB cable rather than using a LAN cable and dealing with IP addresses and DHCP.

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

## Needed hardware
To protect the host computer from electrical faults, ground loops, or high-voltage transients on the DMX line, the circuit implements full galvanic isolation between the USB controller and the DMX transceiver. An isolated DC-DC converter (B0505S-1W) supplies a dedicated, floating 5V rail decoupled from the RP2040’s power domain. Additionally, two high-speed 6N137 optocouplers isolate the logic signals driving the MAX485 transceiver.

- **RP2040** [AliExpress](https://it.aliexpress.com/item/1005006087823796.html)
- **2 MAX485 Module** [AliExpress](https://s.click.aliexpress.com/e/_c3NT3dPz)
- **2 XLR Female Panel Connector** [AliExpress](https://s.click.aliexpress.com/e/_c4q55Lx9)
- **2 6n137** [AliExpress](https://s.click.aliexpress.com/e/_c2JoN82B)
- **B0505S-1W** [AliExpress](https://s.click.aliexpress.com/e/_c4KkuvZv)
- **PCB Board** [AliExpress](https://it.aliexpress.com/item/1005006066751179.html)
- **4 Switches** [AliExpress](https://it.aliexpress.com/item/1005005658595026.html)
- **SSD1206 OLED** [AliExpress](https://s.click.aliexpress.com/e/_c4LZoMDz)
- **Some resistors**
- **3D printer** if you want to print the case, otherwise you can adapt it to a plastic box

### Wiring Diagram
I draw this picture after the making phase of the project, so i'm not 100% shure that it is correct, but it should be
<img width="1593" height="812" alt="image" src="https://github.com/user-attachments/assets/deca0cd0-3b5b-4d48-bdd0-aee6335eb874" />

### 3D Model
The 3D Model is composed by 3 parts and you need m3 screw to assemble it:
- Case
- Top panel
- USB Panel
The position of the USB hole must be aligned with the position of the RP2040 on the PCB.

## Working with QLC+
On QLC+ you have to select Pro Mk2 in the mode menu

<img width="783" height="382" alt="image" src="https://github.com/user-attachments/assets/086ca8a0-3023-481e-b22d-782d8c57f4a3" />

### Issues
The brightness of the OLED screen is not enought with daylight

### Working in progress pictures

A picture of the back of the PCB

<img width="800" height="auto" alt="IMG_20260712_063608" src="https://github.com/user-attachments/assets/3f4ac944-2cf6-496c-8e9f-ad7a728e08b3" />

Board inside the case

<img width="auto" height="800" alt="IMG_20260712_063547" src="https://github.com/user-attachments/assets/71738862-c35d-46e9-b8ec-59844f80d6f3" />

