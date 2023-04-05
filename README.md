## Compiling firmware.bin

1. Download and install official Microsoft's Visual Studio Code, PlatformIO IDE is built on top of it
2. Open VSCode Extension Manager
3. Search for official PlatformIO IDE extension
4. Install PlatformIO IDE.
5. Clone PaigeBraille/firmware repository and open folder on VSCode
6. PlatformIO build button to update `.pio/build/wifi/firmware.bin`
6. Create and upload a config file to tailor the firmware. 

## WebUI

You can find Paige's WebUI [here.](https://github.com/PaigeBraille/paige-web-app)

## Codebase
- Uses C++. Most Arduino libraries are supported as it is the framework used.
- `FluidNC/src/Paige.cpp` contains Paige's important variables that need to be accessed by multiple files.
- `FluidNC/src/Control.cpp` contains GPIO input pin definitions.
- `FluidNC/src/Protocol.cpp` contains GPIO input pin associated events.
- `board_build.partitions` currently set to 16 Meg ESP32.
- `sdcard: frequency_hz:10000000` the SPI speed for the SD card can be configured via the `config.yaml` to work with lower quality SD Card sockets (long wires, external adapters with level translators).

## Credits

This project is an adaptation of [FluidNC](https://github.com/bdring/FluidNC),  a CNC firmware optimized for the ESP32 controller. FluidNC is the next generation of firmware from the creators of Grbl_ESP32. It includes a web based UI and the flexibility to operate a wide variety of machine types.

The original [Grbl](https://github.com/gnea/grbl) is a project by Sungeon (Sonny) Jeon.

The Wifi and WebUI is based on [this project.](https://github.com/luc-github/ESP3D-WEBUI)  
