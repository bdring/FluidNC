# RP2040 Port Implementation Guide

## Architecture Overview

The RP2040 port follows the existing FluidNC architecture with platform-specific implementations organized in this directory.

## File Structure

```
FluidNC/rp2040/
├── README.md                  # RP2040 documentation
├── Platform.h                 # RP2040 platform definitions
├── rp2040.h                   # RP2040 API headers
├── rp2040.cpp                 # RP2040 initialization
├── pico_main.cpp              # Main entry point
├── Arduino.h                  # Arduino API compatibility
├── Arduino.cpp                # Arduino API implementation
├── StepTimer.h/.cpp           # Step timer ISR implementation
├── gpio.cpp                   # GPIO driver
├── uart.cpp                   # UART serial driver
├── spi.cpp                    # SPI communication driver
├── i2c.cpp                    # I2C communication driver
├── wdt.cpp                    # Watchdog timer
├── delay_usecs.cpp            # Microsecond delay
├── littlefs.cpp               # Filesystem support
└── CMakeLists.txt             # CMake build configuration
```

## Key Implementation Details

### Timer Architecture
- Uses RP2040's low-level 1MHz timer
- Alarm 0 generates the step ISR
- Alarms 1-3 are available for other uses
- Interrupt-driven design for minimal latency

### GPIO Management
- All 28 GPIO pins supported (0-27)
- Pull-up/pull-down capabilities utilized
- Interrupt-capable pins for limit switches
- Set/clear operations optimized for stepper pulses

### Serial Communication
- Default UART0 at GPIO0/1: 115200 baud
- Optional UART1 at GPIO4/5 for secondary use
- Non-blocking receive via polling or interrupts
- Hardware flow control optional

### SPI Configuration
- 2 independent SPI controllers
- Default SPI0 for stepper drivers
- Default SPI1 available for expansion
- Chip select handled as GPIO (GPIO17/13)

### I2C Support
- 2 I2C buses available
- Default 100kHz standard mode
- Probe mechanism for device detection
- Write-read operation for register access

## Integration with Core FluidNC

The RP2040 port integrates with FluidNC's core through:

1. **Platform Abstraction Layer**
   - `Platform.h` defines MCU-specific constants
   - Alternative stepping engine implementations
   - Memory configuration

2. **Stepping System**
   - `StepTimer.cpp` provides interrupt-driven stepping
   - Direct GPIO access minimizes latency
   - Microsecond-precision timing

3. **I/O Drivers**
   - UART channel for serial protocol
   - SPI for stepper driver communication
   - I2C for sensor and expander boards
   - GPIO for discrete I/O

4. **Machine Configuration**
   - Standard YAML configuration support
   - Pin definitions via machine config
   - Feature flags for RP2040 limitations

## Compilation Process

1. **Preprocessing**: Platform.h and rp2040.h define RP2040-specific configuration
2. **Compilation**: Source files compiled with RP2040-specific flags
3. **Linking**: Pico SDK libraries linked in
4. **Binary Generation**: `.uf2` firmware image created for upload

## Performance Characteristics

- **Step Frequency**: Up to 100kHz stepping rate
- **ISR Latency**: < 1µs from alarm to handler execution
- **Stepping Jitter**: Minimal due to interrupt-driven design
- **Memory Usage**: ~150KB firmware + 100KB config/runtime

## Testing Strategy

1. **Basic Functionality**
   - GPIO toggling at various rates
   - Serial communication at 115200 baud
   - Timer ISR accuracy

2. **Integration Tests**
   - Stepping with PLanner
   - Multi-axis coordination
   - Interrupt handler nesting

3. **Hardware Tests**
   - Real stepper driver operation
   - Limit switch debouncing
   - PWM spindle control

## Future Enhancements

1. **WiFi Support (Pico W)**
   - CYW43 driver integration
   - Async web server (limited resources)
   - WebSocket communication

2. **Performance**
   - Optional overclocking (up to 270MHz)
   - Dual-core utilization
   - Optimized stepping engine

3. **Features**
   - SD card support via SPI
   - Real-time clock (RTC)
   - Advanced sensor integration

## Debugging

### Serial Output
Use PlatformIO's serial monitor to view debug output and command responses:
```bash
platformio run -e rp2040_noradio -t monitor
```

### GPIO Analysis
- Use a logic analyzer on GPIO pins to verify signal timing
- Measure step pulse width and frequency
- Check direction signal timing

### ISR Timing
- Add GPIO toggle for ISR entry/exit to measure timing
- Monitor timer alarm accuracy
- Check for ISR handler overflow

## Building from Source

See the main FluidNC build instructions and PlatformIO configuration in `platformio.ini`.

The RP2040 build uses the Raspberry Pi Pico SDK, which is automatically installed by PlatformIO when targeting the rp2040 platform.
