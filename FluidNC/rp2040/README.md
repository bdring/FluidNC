# RP2040 Support for FluidNC

This directory contains the RP2040 (Raspberry Pi Pico) port of FluidNC.

## Overview

The RP2040 port brings FluidNC CNC firmware support to the Raspberry Pi Pico microcontroller, enabling affordable 3-axis and multi-axis CNC machines with web-based control (on Pico W).

## Hardware Support

### Supported Boards
- **Raspberry Pi Pico** (standard, 2MB flash, no wireless)
- **Raspberry Pi Pico W** (WiFi-enabled variant)

### Features
- ✅ Stepping motor control (up to 6 axes)
- ✅ UART serial communication
- ✅ SPI communication (for stepper drivers like TMC2209)
- ✅ I2C support (for sensors, I/O expanders)
- ✅ GPIO-based limit switches and control pins
- ✅ PWM spindle control
- ✅ LittleFS filesystem support
- 🟡 WiFi support (Pico W with limited web UI)
- 🔲 Bluetooth support (not yet implemented)

## Pin Configuration

### Default Pin Assignments

#### UART
- **UART0**: GPIO0 (TX), GPIO1 (RX) - Console/main serial
- **UART1**: GPIO4 (TX), GPIO5 (RX) - Optional secondary

#### SPI
- **SPI0**: GPIO18 (SCK), GPIO19 (MOSI), GPIO16 (MISO), GPIO17 (CS)
- **SPI1**: GPIO10 (SCK), GPIO11 (MOSI), GPIO12 (MISO), GPIO13 (CS)

#### I2C
- **I2C0**: GPIO4 (SDA), GPIO5 (SCL)
- **I2C1**: GPIO2 (SDA), GPIO3 (SCL)

#### Stepper Motors (Default)
- **X Axis**: GPIO2 (Step), GPIO3 (Dir)
- **Y Axis**: GPIO4 (Step), GPIO5 (Dir)
- **Z Axis**: GPIO6 (Step), GPIO7 (Dir)

#### Control Pins
- **Probe**: GPIO28
- **Feed Hold**: GPIO26
- **Cycle Start**: GPIO27
- **Spindle PWM**: GPIO15
- **Flood Coolant**: GPIO14

## Building

### Prerequisites
- PlatformIO
- Raspberry Pi Pico SDK (automatically installed by PlatformIO)

### Build Commands

```bash
# Build for standard Pico (no wireless)
platformio run --environment rp2040_noradio

# Build for Pico W (with WiFi)
platformio run --environment rp2040_wifi

# Upload to device
platformio run --environment rp2040_noradio --target upload

# Monitor serial output
platformio run --environment rp2040_noradio --target monitor
```

## Configuration

Machine configuration is done via YAML files in the `FluidNC/data` directory. See `example_configs/rp2040_3axis.yaml` for an example configuration.

### Key Configuration Considerations for RP2040

1. **Stepping Engine**: Uses "Timed" stepping engine
2. **GPIO Pins**: RP2040 has 28 GPIO pins (0-27)
3. **UART Speed**: Default 115200 baud (the only UART available initially)
4. **SPI Devices**: Limited to 2 SPI instances
5. **I2C Devices**: 2 I2C instances available
6. **Memory**: Limited to 264KB SRAM (plan accordingly)
7. **Flash**: 2MB typical (128KB boot, 256KB filesystem, ~1.6MB firmware)

## Uploading

### Via USB
1. Connect Pico to computer via USB while holding the BOOTSEL button
2. Copy the compiled `.uf2` file to the RPI-RP2 drive that appears
3. Pico automatically resets and runs the new firmware

### Via Picoprobe
1. Connect a Picoprobe debugger to the Pico
2. Configure `upload_protocol = picoprobe` in platformio.ini
3. Use PlatformIO upload

## Development Notes

### Memory Constraints
The RP2040 has limited SRAM (264KB). Some features from the ESP32 version may need to be disabled:
- WebUI is minimal or disabled (limited RAM for web server)
- Motor driver support is limited to essentials
- Configuration tree may be simplified

### Timer Implementation
- Uses RP2040's 1MHz low-level timer
- ISR-driven stepping at microsecond precision
- Alarm-based interrupt system

### Filesystem
- LittleFS support for configuration and file storage
- Approximately 256KB allocated for filesystem
- Configuration stored in YAML files on flash

## Limitations

1. **No Bluetooth** - WiFi is available on Pico W, but not currently exposed
2. **Limited WebUI** - Memory constraints limit full ESP32 WebUI functionality
3. **Single UART** - Primary console on UART0; limited secondary UART support
4. **No Motor Power** - Pico cannot drive motors directly; requires external stepper drivers
5. **Reduced Feature Set** - Some advanced features (VFD control, extensive sensor support) may be limited

## Troubleshooting

### Board Not Detected
- Ensure you have the Pico board libraries installed via PlatformIO
- Try placing the board in bootloader mode (hold BOOTSEL while connecting)

### Upload Fails
- Check that the serial port is correctly configured in platformio.ini
- Ensure no other terminal/monitor is open to the serial port

### Stepping Not Working
- Verify GPIO pins are correctly configured in your machine YAML
- Check that the external stepper driver is properly connected and powered
- Verify step and direction signals with an oscilloscope if possible

## Flash Memory Layout

The RP2040 has 2MB of flash memory (0x000000 - 0x200000) organized as follows:

```
0x000000 ┌─────────────────────────────────┐
         │                                 │
         │      Application Firmware       │  ~1.984 MB
         │   (FluidNC + FreeRTOS kernel)   │ (0x000000 - 0x1EFFFF)
         │                                 │
0x1F0000 ├─────────────────────────────────┤
         │      LittleFS Filesystem        │  60 KB
         │   (config/SD card files)        │ (0x1F0000 - 0x1FEFFF)
0x1FF000 ├─────────────────────────────────┤
         │  Non-Volatile Storage (NVS)     │  4 KB
         │    (machine settings, state)    │ (0x1FF000 - 0x1FFFFF)
0x200000 └─────────────────────────────────┘
```

**Key Points:**
- Firmware is loaded to address 0x000000 and executes via XIP (eXecute In Place)
- LittleFS requires minimum 60KB for reliable operation
- NVS reserves 4KB at the end for persistent configuration storage
- The bootloader (Pico SDK runtime) is part of the main application image

**Configuration via platformio.ini:**
```ini
board_build.filesystem_size = 60k           # LittleFS size
-DNVS_FLASH_OFFSET=0x1FF000                # NVS start address
-DNVS_SECTOR_SIZE=0x1000                   # NVS size (4KB)
```

## Contributing

Contributions are welcome! Areas for improvement:
- Enhanced WiFi support for Pico W
- Additional motor driver support
- Sensor integration examples
- Performance optimizations

## References

- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Raspberry Pi Pico Documentation](https://www.raspberrypi.com/documentation/microcontrollers/pico.html)
- [Pico-SDK GitHub](https://github.com/raspberrypi/pico-sdk)
- [FluidNC Main Repository](https://github.com/bdring/FluidNC)
