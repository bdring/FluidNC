#include "TS35DisplayDriver.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <Arduino.h>
#include <pgmspace.h>
#include <sdkconfig.h>

// Machine::SPIBus opens the shared bus on HSPI_HOST; spi.cpp maps that name the
// same way for the ESP32-S3, where the constant is spelled SPI2_HOST.
#ifdef CONFIG_IDF_TARGET_ESP32S3
#    define HSPI_HOST SPI2_HOST
#endif

namespace ts35 {

    namespace {

        constexpr std::uint8_t CmdSoftwareReset = 0x01;
        constexpr std::uint8_t CmdSleepOut      = 0x11;
        constexpr std::uint8_t CmdInvertOff     = 0x20;
        constexpr std::uint8_t CmdInvertOn      = 0x21;
        constexpr std::uint8_t CmdDisplayOn     = 0x29;
        constexpr std::uint8_t CmdColumnAddress = 0x2A;
        constexpr std::uint8_t CmdPageAddress   = 0x2B;
        constexpr std::uint8_t CmdMemoryWrite   = 0x2C;
        constexpr std::uint8_t CmdMemoryAccess  = 0x36;
        constexpr std::uint8_t CmdPixelFormat   = 0x3A;

        constexpr std::uint8_t TouchReadX  = 0xD0;
        constexpr std::uint8_t TouchReadY  = 0x90;
        constexpr std::uint8_t TouchReadZ1 = 0xB0;
        constexpr std::uint8_t TouchReadZ2 = 0xC0;

        constexpr std::uint8_t MaximumTouchSamples = 9;

        // One solid-fill burst.  512 bytes keeps the transfer well inside the 4000-byte
        // maximum that spi_init_bus() configures for the shared bus, and keeps the
        // scratch buffer small enough to live on the caller's stack, which is where the
        // SPI driver needs it for DMA.
        constexpr std::uint32_t PixelsPerBurst = 256;

        // Classic Adafruit_GFX/TFT_eSPI 5x7 font, ASCII 0x20 through 0x7e.  Each
        // character is stored as five vertical columns, least-significant bit first.
        static const std::uint8_t Font5x7[] PROGMEM = {
            0x00, 0x00, 0x00, 0x00, 0x00,  // space
            0x00, 0x00, 0x5F, 0x00, 0x00,  // !
            0x00, 0x07, 0x00, 0x07, 0x00,  // "
            0x14, 0x7F, 0x14, 0x7F, 0x14,  // #
            0x24, 0x2A, 0x7F, 0x2A, 0x12,  // $
            0x23, 0x13, 0x08, 0x64, 0x62,  // %
            0x36, 0x49, 0x56, 0x20, 0x50,  // &
            0x00, 0x08, 0x07, 0x03, 0x00,  // '
            0x00, 0x1C, 0x22, 0x41, 0x00,  // (
            0x00, 0x41, 0x22, 0x1C, 0x00,  // )
            0x2A, 0x1C, 0x7F, 0x1C, 0x2A,  // *
            0x08, 0x08, 0x3E, 0x08, 0x08,  // +
            0x00, 0x80, 0x70, 0x30, 0x00,  // ,
            0x08, 0x08, 0x08, 0x08, 0x08,  // -
            0x00, 0x00, 0x60, 0x60, 0x00,  // .
            0x20, 0x10, 0x08, 0x04, 0x02,  // /
            0x3E, 0x51, 0x49, 0x45, 0x3E,  // 0
            0x00, 0x42, 0x7F, 0x40, 0x00,  // 1
            0x72, 0x49, 0x49, 0x49, 0x46,  // 2
            0x21, 0x41, 0x49, 0x4D, 0x33,  // 3
            0x18, 0x14, 0x12, 0x7F, 0x10,  // 4
            0x27, 0x45, 0x45, 0x45, 0x39,  // 5
            0x3C, 0x4A, 0x49, 0x49, 0x31,  // 6
            0x41, 0x21, 0x11, 0x09, 0x07,  // 7
            0x36, 0x49, 0x49, 0x49, 0x36,  // 8
            0x46, 0x49, 0x49, 0x29, 0x1E,  // 9
            0x00, 0x00, 0x14, 0x00, 0x00,  // :
            0x00, 0x40, 0x34, 0x00, 0x00,  // ;
            0x00, 0x08, 0x14, 0x22, 0x41,  // <
            0x14, 0x14, 0x14, 0x14, 0x14,  // =
            0x00, 0x41, 0x22, 0x14, 0x08,  // >
            0x02, 0x01, 0x59, 0x09, 0x06,  // ?
            0x3E, 0x41, 0x5D, 0x59, 0x4E,  // @
            0x7C, 0x12, 0x11, 0x12, 0x7C,  // A
            0x7F, 0x49, 0x49, 0x49, 0x36,  // B
            0x3E, 0x41, 0x41, 0x41, 0x22,  // C
            0x7F, 0x41, 0x41, 0x41, 0x3E,  // D
            0x7F, 0x49, 0x49, 0x49, 0x41,  // E
            0x7F, 0x09, 0x09, 0x09, 0x01,  // F
            0x3E, 0x41, 0x41, 0x51, 0x73,  // G
            0x7F, 0x08, 0x08, 0x08, 0x7F,  // H
            0x00, 0x41, 0x7F, 0x41, 0x00,  // I
            0x20, 0x40, 0x41, 0x3F, 0x01,  // J
            0x7F, 0x08, 0x14, 0x22, 0x41,  // K
            0x7F, 0x40, 0x40, 0x40, 0x40,  // L
            0x7F, 0x02, 0x1C, 0x02, 0x7F,  // M
            0x7F, 0x04, 0x08, 0x10, 0x7F,  // N
            0x3E, 0x41, 0x41, 0x41, 0x3E,  // O
            0x7F, 0x09, 0x09, 0x09, 0x06,  // P
            0x3E, 0x41, 0x51, 0x21, 0x5E,  // Q
            0x7F, 0x09, 0x19, 0x29, 0x46,  // R
            0x26, 0x49, 0x49, 0x49, 0x32,  // S
            0x03, 0x01, 0x7F, 0x01, 0x03,  // T
            0x3F, 0x40, 0x40, 0x40, 0x3F,  // U
            0x1F, 0x20, 0x40, 0x20, 0x1F,  // V
            0x3F, 0x40, 0x38, 0x40, 0x3F,  // W
            0x63, 0x14, 0x08, 0x14, 0x63,  // X
            0x03, 0x04, 0x78, 0x04, 0x03,  // Y
            0x61, 0x59, 0x49, 0x4D, 0x43,  // Z
            0x00, 0x7F, 0x41, 0x41, 0x41,  // [
            0x02, 0x04, 0x08, 0x10, 0x20,  // backslash
            0x00, 0x41, 0x41, 0x41, 0x7F,  // ]
            0x04, 0x02, 0x01, 0x02, 0x04,  // ^
            0x40, 0x40, 0x40, 0x40, 0x40,  // _
            0x00, 0x03, 0x07, 0x08, 0x00,  // `
            0x20, 0x54, 0x54, 0x78, 0x40,  // a
            0x7F, 0x28, 0x44, 0x44, 0x38,  // b
            0x38, 0x44, 0x44, 0x44, 0x28,  // c
            0x38, 0x44, 0x44, 0x28, 0x7F,  // d
            0x38, 0x54, 0x54, 0x54, 0x18,  // e
            0x00, 0x08, 0x7E, 0x09, 0x02,  // f
            0x18, 0xA4, 0xA4, 0x9C, 0x78,  // g
            0x7F, 0x08, 0x04, 0x04, 0x78,  // h
            0x00, 0x44, 0x7D, 0x40, 0x00,  // i
            0x20, 0x40, 0x40, 0x3D, 0x00,  // j
            0x7F, 0x10, 0x28, 0x44, 0x00,  // k
            0x00, 0x41, 0x7F, 0x40, 0x00,  // l
            0x7C, 0x04, 0x78, 0x04, 0x78,  // m
            0x7C, 0x08, 0x04, 0x04, 0x78,  // n
            0x38, 0x44, 0x44, 0x44, 0x38,  // o
            0xFC, 0x18, 0x24, 0x24, 0x18,  // p
            0x18, 0x24, 0x24, 0x18, 0xFC,  // q
            0x7C, 0x08, 0x04, 0x04, 0x08,  // r
            0x48, 0x54, 0x54, 0x54, 0x24,  // s
            0x04, 0x04, 0x3F, 0x44, 0x24,  // t
            0x3C, 0x40, 0x40, 0x20, 0x7C,  // u
            0x1C, 0x20, 0x40, 0x20, 0x1C,  // v
            0x3C, 0x40, 0x30, 0x40, 0x3C,  // w
            0x44, 0x28, 0x10, 0x28, 0x44,  // x
            0x4C, 0x90, 0x90, 0x90, 0x7C,  // y
            0x44, 0x64, 0x54, 0x4C, 0x44,  // z
            0x00, 0x08, 0x36, 0x41, 0x00,  // {
            0x00, 0x00, 0x77, 0x00, 0x00,  // |
            0x00, 0x41, 0x36, 0x08, 0x00,  // }
            0x02, 0x01, 0x02, 0x04, 0x02,  // ~
        };

        static_assert(sizeof(Font5x7) == 95U * 5U, "5x7 font table must cover printable ASCII");

        bool pinIsAssigned(std::int8_t pin) {
            return pin >= 0;
        }

    }  // namespace

    TS35DisplayDriver::TS35DisplayDriver() : config_(), lcd_(nullptr), touch_(nullptr), initialized_(false) {}

    TS35DisplayDriver::TS35DisplayDriver(const Config& config) : config_(config), lcd_(nullptr), touch_(nullptr), initialized_(false) {}

    TS35DisplayDriver::~TS35DisplayDriver() {
        end();
    }

    void TS35DisplayDriver::setConfig(const Config& config) {
        end();
        config_ = config;
    }

    void TS35DisplayDriver::end() {
        if (initialized_) {
            setBacklight(false);
        }
        if (lcd_ != nullptr) {
            spi_bus_remove_device(lcd_);
            lcd_ = nullptr;
        }
        if (touch_ != nullptr) {
            spi_bus_remove_device(touch_);
            touch_ = nullptr;
        }
        initialized_ = false;
    }

    bool TS35DisplayDriver::validConfig() const {
        if (config_.lcdClockHz == 0 || config_.touchClockHz == 0) {
            return false;
        }

        const std::int8_t requiredPins[] = {
            config_.pins.tftCs,
            config_.pins.tftDc,
            config_.pins.touchCs,
        };

        for (std::size_t index = 0; index < sizeof(requiredPins) / sizeof(requiredPins[0]); ++index) {
            if (!pinIsAssigned(requiredPins[index])) {
                return false;
            }
            for (std::size_t other = index + 1; other < sizeof(requiredPins) / sizeof(requiredPins[0]); ++other) {
                if (requiredPins[index] == requiredPins[other]) {
                    return false;
                }
            }
        }

        const std::int8_t optionalPins[] = { config_.pins.tftReset, config_.pins.backlight };
        for (std::size_t index = 0; index < sizeof(optionalPins) / sizeof(optionalPins[0]); ++index) {
            if (!pinIsAssigned(optionalPins[index])) {
                continue;
            }
            for (std::size_t other = 0; other < sizeof(requiredPins) / sizeof(requiredPins[0]); ++other) {
                if (optionalPins[index] == requiredPins[other]) {
                    return false;
                }
            }
            if (index > 0 && optionalPins[index] == optionalPins[index - 1]) {
                return false;
            }
        }

        const TouchCalibration& touch = config_.touch;
        return touch.xMin < touch.xMax && touch.yMin < touch.yMax && touch.samples > 0 && touch.samples <= MaximumTouchSamples;
    }

    bool TS35DisplayDriver::addDevices() {
        // Manual chip select: the two devices need CS held across a command and its
        // parameters, and across a whole touch conversion sequence, which is longer
        // than one spi_transaction_t.
        spi_device_interface_config_t lcdConfig = {};
        lcdConfig.clock_speed_hz                = static_cast<int>(config_.lcdClockHz);
        lcdConfig.mode                          = 0;
        lcdConfig.spics_io_num                  = -1;
        lcdConfig.queue_size                    = 1;
        // Above 26.6 MHz the full-duplex input of a GPIO-matrix pin needs a dummy
        // phase to stay valid.  Nothing is ever read back from the LCD, so the
        // driver is told to skip it rather than refuse the clock.
        lcdConfig.flags = SPI_DEVICE_NO_DUMMY;

        spi_device_interface_config_t touchConfig = {};
        touchConfig.clock_speed_hz                = static_cast<int>(config_.touchClockHz);
        touchConfig.mode                          = 0;
        touchConfig.spics_io_num                  = -1;
        touchConfig.queue_size                    = 1;

        if (spi_bus_add_device(HSPI_HOST, &lcdConfig, &lcd_) != ESP_OK) {
            lcd_ = nullptr;
            return false;
        }
        if (spi_bus_add_device(HSPI_HOST, &touchConfig, &touch_) != ESP_OK) {
            touch_ = nullptr;
            spi_bus_remove_device(lcd_);
            lcd_ = nullptr;
            return false;
        }
        return true;
    }

    bool TS35DisplayDriver::begin() {
        end();

        if (!validConfig()) {
            return false;
        }

        pinMode(config_.pins.tftCs, OUTPUT);
        pinMode(config_.pins.tftDc, OUTPUT);
        pinMode(config_.pins.touchCs, OUTPUT);
        digitalWrite(config_.pins.tftCs, HIGH);
        digitalWrite(config_.pins.tftDc, HIGH);
        digitalWrite(config_.pins.touchCs, HIGH);

        if (pinIsAssigned(config_.pins.backlight)) {
            pinMode(config_.pins.backlight, OUTPUT);
            const bool offLevel = config_.backlightActiveLow;
            digitalWrite(config_.pins.backlight, offLevel ? HIGH : LOW);
        }

        if (pinIsAssigned(config_.pins.tftReset)) {
            pinMode(config_.pins.tftReset, OUTPUT);
            digitalWrite(config_.pins.tftReset, HIGH);
        }

        // The bus itself belongs to the machine's `spi:` section and is already
        // open by the time modules are initialised; this only attaches to it, and
        // fails cleanly when the section is missing.
        if (!addDevices()) {
            return false;
        }

        if (pinIsAssigned(config_.pins.tftReset)) {
            delay(5);
            digitalWrite(config_.pins.tftReset, LOW);
            delay(20);
            digitalWrite(config_.pins.tftReset, HIGH);
            delay(120);
        }

        // The values below are the ST7796 sequence carried by the public MKS
        // DLC32/TFT_eSPI source.  The MADCTL value 0x28 is MV|BGR and exposes the
        // controller's native 320x480 RAM as 480x320 landscape coordinates.
        writeCommand(CmdSoftwareReset);
        delay(120);
        writeCommand(CmdSleepOut);
        delay(20);

        const std::uint8_t commandSetOne[] = { 0xC3 };
        const std::uint8_t commandSetTwo[] = { 0x96 };
        writeCommand(0xF0, commandSetOne, sizeof(commandSetOne));
        writeCommand(0xF0, commandSetTwo, sizeof(commandSetTwo));

        const std::uint8_t memoryAccess[] = { 0x28 };
        const std::uint8_t pixelFormat[]  = { 0x55 };
        const std::uint8_t inversion[]    = { 0x01 };
        const std::uint8_t entryMode[]    = { 0xC6 };
        writeCommand(CmdMemoryAccess, memoryAccess, sizeof(memoryAccess));
        writeCommand(CmdPixelFormat, pixelFormat, sizeof(pixelFormat));
        writeCommand(0xB4, inversion, sizeof(inversion));
        writeCommand(0xB7, entryMode, sizeof(entryMode));

        const std::uint8_t displayOutput[] = { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 };
        const std::uint8_t powerTwo[]      = { 0x06 };
        const std::uint8_t powerThree[]    = { 0xA7 };
        const std::uint8_t vcom[]          = { 0x18 };
        writeCommand(0xE8, displayOutput, sizeof(displayOutput));
        writeCommand(0xC1, powerTwo, sizeof(powerTwo));
        writeCommand(0xC2, powerThree, sizeof(powerThree));
        writeCommand(0xC5, vcom, sizeof(vcom));

        const std::uint8_t positiveGamma[] = {
            0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B,
        };
        const std::uint8_t negativeGamma[] = {
            0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B,
        };
        writeCommand(0xE0, positiveGamma, sizeof(positiveGamma));
        writeCommand(0xE1, negativeGamma, sizeof(negativeGamma));

        const std::uint8_t commandSetExitOne[] = { 0x3C };
        const std::uint8_t commandSetExitTwo[] = { 0x69 };
        writeCommand(0xF0, commandSetExitOne, sizeof(commandSetExitOne));
        writeCommand(0xF0, commandSetExitTwo, sizeof(commandSetExitTwo));

        writeCommand(config_.invertDisplay ? CmdInvertOn : CmdInvertOff);
        delay(120);
        writeCommand(CmdDisplayOn);
        delay(20);

        initialized_ = true;
        fillScreen(0x0000);  // black
        setBacklight(true);
        return true;
    }

    void TS35DisplayDriver::setBacklight(bool on) {
        if (!pinIsAssigned(config_.pins.backlight)) {
            return;
        }
        const bool activeLevel = !config_.backlightActiveLow;
        digitalWrite(config_.pins.backlight, on ? activeLevel : !activeLevel);
    }

    void TS35DisplayDriver::writeBytes(spi_device_handle_t device, const std::uint8_t* data, std::size_t length) {
        // The SPI driver wants a DMA-capable source, which a stack buffer is and a
        // caller's const array in flash may not be, so the bytes are copied.
        std::uint8_t buffer[32];
        while (length != 0) {
            const std::size_t chunk = length < sizeof(buffer) ? length : sizeof(buffer);
            std::memcpy(buffer, data, chunk);
            spi_transaction_t transaction = {};
            transaction.length            = chunk * 8U;
            transaction.tx_buffer         = buffer;
            spi_device_polling_transmit(device, &transaction);
            data += chunk;
            length -= chunk;
        }
    }

    void TS35DisplayDriver::beginLcdTransaction() {
        spi_device_acquire_bus(lcd_, portMAX_DELAY);
        digitalWrite(config_.pins.touchCs, HIGH);
        digitalWrite(config_.pins.tftCs, LOW);
    }

    void TS35DisplayDriver::endLcdTransaction() {
        digitalWrite(config_.pins.tftCs, HIGH);
        digitalWrite(config_.pins.tftDc, HIGH);
        spi_device_release_bus(lcd_);
    }

    void TS35DisplayDriver::beginTouchTransaction() {
        spi_device_acquire_bus(touch_, portMAX_DELAY);
        digitalWrite(config_.pins.tftCs, HIGH);
        digitalWrite(config_.pins.touchCs, LOW);
    }

    void TS35DisplayDriver::endTouchTransaction() {
        digitalWrite(config_.pins.touchCs, HIGH);
        spi_device_release_bus(touch_);
    }

    void TS35DisplayDriver::writeCommand(std::uint8_t command, const std::uint8_t* data, std::uint8_t length) {
        beginLcdTransaction();
        writeCommandInTransaction(command, data, length);
        endLcdTransaction();
    }

    void TS35DisplayDriver::writeCommandInTransaction(std::uint8_t command, const std::uint8_t* data, std::uint8_t length) {
        digitalWrite(config_.pins.tftDc, LOW);
        writeBytes(lcd_, &command, 1);
        if (data != nullptr && length != 0) {
            digitalWrite(config_.pins.tftDc, HIGH);
            writeBytes(lcd_, data, length);
        }
        digitalWrite(config_.pins.tftDc, HIGH);
    }

    void TS35DisplayDriver::setAddressWindowInTransaction(std::uint16_t x, std::uint16_t y, std::uint16_t widthValue, std::uint16_t heightValue) {
        const std::uint16_t xEnd      = static_cast<std::uint16_t>(x + widthValue - 1U);
        const std::uint16_t yEnd      = static_cast<std::uint16_t>(y + heightValue - 1U);
        const std::uint8_t  columns[] = {
            static_cast<std::uint8_t>(x >> 8),
            static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(xEnd >> 8),
            static_cast<std::uint8_t>(xEnd),
        };
        const std::uint8_t pages[] = {
            static_cast<std::uint8_t>(y >> 8),
            static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(yEnd >> 8),
            static_cast<std::uint8_t>(yEnd),
        };
        writeCommandInTransaction(CmdColumnAddress, columns, sizeof(columns));
        writeCommandInTransaction(CmdPageAddress, pages, sizeof(pages));
        writeCommandInTransaction(CmdMemoryWrite);
    }

    void TS35DisplayDriver::writeSolidPixelsInTransaction(std::uint16_t color, std::uint32_t count) {
        digitalWrite(config_.pins.tftDc, HIGH);
        const std::uint32_t burstPixels = count < PixelsPerBurst ? count : PixelsPerBurst;
        std::uint8_t        burst[PixelsPerBurst * 2U];
        for (std::uint32_t index = 0; index < burstPixels; ++index) {
            burst[index * 2U]      = static_cast<std::uint8_t>(color >> 8);
            burst[index * 2U + 1U] = static_cast<std::uint8_t>(color);
        }
        while (count != 0) {
            const std::uint32_t pixels      = count < burstPixels ? count : burstPixels;
            spi_transaction_t   transaction = {};
            transaction.length              = pixels * 16U;
            transaction.tx_buffer           = burst;
            spi_device_polling_transmit(lcd_, &transaction);
            count -= pixels;
        }
    }

    bool TS35DisplayDriver::clipRect(std::int16_t& x, std::int16_t& y, std::int16_t& widthValue, std::int16_t& heightValue) const {
        if (widthValue <= 0 || heightValue <= 0) {
            return false;
        }

        std::int32_t left   = x;
        std::int32_t top    = y;
        std::int32_t right  = left + widthValue;
        std::int32_t bottom = top + heightValue;

        if (right <= 0 || bottom <= 0 || left >= Width || top >= Height) {
            return false;
        }
        if (left < 0) {
            left = 0;
        }
        if (top < 0) {
            top = 0;
        }
        if (right > Width) {
            right = Width;
        }
        if (bottom > Height) {
            bottom = Height;
        }

        x           = static_cast<std::int16_t>(left);
        y           = static_cast<std::int16_t>(top);
        widthValue  = static_cast<std::int16_t>(right - left);
        heightValue = static_cast<std::int16_t>(bottom - top);
        return widthValue > 0 && heightValue > 0;
    }

    void TS35DisplayDriver::fillRectInTransaction(
        std::int16_t x, std::int16_t y, std::int16_t widthValue, std::int16_t heightValue, std::uint16_t color) {
        if (!clipRect(x, y, widthValue, heightValue)) {
            return;
        }
        setAddressWindowInTransaction(static_cast<std::uint16_t>(x),
                                      static_cast<std::uint16_t>(y),
                                      static_cast<std::uint16_t>(widthValue),
                                      static_cast<std::uint16_t>(heightValue));
        writeSolidPixelsInTransaction(color, static_cast<std::uint32_t>(widthValue) * static_cast<std::uint32_t>(heightValue));
    }

    void TS35DisplayDriver::fillScreen(std::uint16_t color) {
        fillRect(0, 0, Width, Height, color);
    }

    void TS35DisplayDriver::fillRect(std::int16_t x, std::int16_t y, std::int16_t widthValue, std::int16_t heightValue, std::uint16_t color) {
        if (!initialized_ || !clipRect(x, y, widthValue, heightValue)) {
            return;
        }
        beginLcdTransaction();
        setAddressWindowInTransaction(static_cast<std::uint16_t>(x),
                                      static_cast<std::uint16_t>(y),
                                      static_cast<std::uint16_t>(widthValue),
                                      static_cast<std::uint16_t>(heightValue));
        writeSolidPixelsInTransaction(color, static_cast<std::uint32_t>(widthValue) * static_cast<std::uint32_t>(heightValue));
        endLcdTransaction();
    }

    void TS35DisplayDriver::drawRect(std::int16_t x, std::int16_t y, std::int16_t widthValue, std::int16_t heightValue, std::uint16_t color) {
        if (!initialized_ || widthValue <= 0 || heightValue <= 0) {
            return;
        }

        beginLcdTransaction();
        fillRectInTransaction(x, y, widthValue, 1, color);
        if (heightValue > 1) {
            fillRectInTransaction(x, static_cast<std::int16_t>(y + heightValue - 1), widthValue, 1, color);
        }
        if (heightValue > 2) {
            fillRectInTransaction(x, static_cast<std::int16_t>(y + 1), 1, static_cast<std::int16_t>(heightValue - 2), color);
            if (widthValue > 1) {
                fillRectInTransaction(static_cast<std::int16_t>(x + widthValue - 1),
                                      static_cast<std::int16_t>(y + 1),
                                      1,
                                      static_cast<std::int16_t>(heightValue - 2),
                                      color);
            }
        }
        endLcdTransaction();
    }

    void TS35DisplayDriver::drawGlyphInTransaction(
        std::int16_t x, std::int16_t y, char character, std::uint16_t foreground, std::uint16_t background, std::uint8_t scale) {
        std::uint8_t code = static_cast<std::uint8_t>(character);
        if (code < 0x20 || code > 0x7E) {
            code = static_cast<std::uint8_t>('?');
        }

        fillRectInTransaction(x, y, static_cast<std::int16_t>(6U * scale), static_cast<std::int16_t>(8U * scale), background);

        const std::size_t glyphOffset = static_cast<std::size_t>(code - 0x20U) * 5U;
        for (std::uint8_t row = 0; row < 8; ++row) {
            std::uint8_t column = 0;
            while (column < 5) {
                while (column < 5) {
                    const std::uint8_t bits = pgm_read_byte(Font5x7 + glyphOffset + column);
                    if ((bits & (1U << row)) != 0) {
                        break;
                    }
                    ++column;
                }
                const std::uint8_t runStart = column;
                while (column < 5) {
                    const std::uint8_t bits = pgm_read_byte(Font5x7 + glyphOffset + column);
                    if ((bits & (1U << row)) == 0) {
                        break;
                    }
                    ++column;
                }
                if (runStart < column) {
                    fillRectInTransaction(static_cast<std::int16_t>(x + static_cast<std::int16_t>(runStart * scale)),
                                          static_cast<std::int16_t>(y + static_cast<std::int16_t>(row * scale)),
                                          static_cast<std::int16_t>((column - runStart) * scale),
                                          scale,
                                          foreground);
                }
            }
        }
    }

    void TS35DisplayDriver::drawText(
        std::int16_t x, std::int16_t y, const char* text, std::uint16_t foreground, std::uint16_t background, std::uint8_t scale) {
        if (!initialized_ || text == nullptr || scale == 0) {
            return;
        }

        const std::int32_t originX = x;
        std::int32_t       cursorX = x;
        std::int32_t       cursorY = y;
        const std::int32_t advance = 6 * static_cast<std::int32_t>(scale);
        const std::int32_t line    = 8 * static_cast<std::int32_t>(scale);

        beginLcdTransaction();
        while (*text != '\0') {
            const char character = *text++;
            if (character == '\r') {
                continue;
            }
            if (character == '\n') {
                cursorX = originX;
                cursorY += line;
                if (cursorY >= Height) {
                    break;
                }
                continue;
            }

            if (cursorX >= Width) {
                break;
            }
            if (cursorX + advance > 0 && cursorY + line > 0 && cursorY < Height) {
                drawGlyphInTransaction(
                    static_cast<std::int16_t>(cursorX), static_cast<std::int16_t>(cursorY), character, foreground, background, scale);
            }
            cursorX += advance;
        }
        endLcdTransaction();
    }

    std::uint16_t TS35DisplayDriver::readAdc12InTransaction(std::uint8_t command) {
        std::uint8_t      out[3]      = { command, 0x00, 0x00 };
        std::uint8_t      in[3]       = { 0x00, 0x00, 0x00 };
        spi_transaction_t transaction = {};
        transaction.length            = sizeof(out) * 8U;
        transaction.tx_buffer         = out;
        transaction.rx_buffer         = in;
        spi_device_polling_transmit(touch_, &transaction);
        const std::uint16_t value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8) | in[2];
        return static_cast<std::uint16_t>((value >> 3) & 0x0FFFU);
    }

    std::uint16_t TS35DisplayDriver::readPressureInTransaction() {
        const std::uint16_t z1 = readAdc12InTransaction(TouchReadZ1);
        const std::uint16_t z2 = readAdc12InTransaction(TouchReadZ2);
        if (z1 == 0) {
            return 0;
        }
        std::int32_t pressure = 4095 + static_cast<std::int32_t>(z1) - static_cast<std::int32_t>(z2);
        if (pressure < 0) {
            pressure = 0;
        } else if (pressure > 0xFFFF) {
            pressure = 0xFFFF;
        }
        return static_cast<std::uint16_t>(pressure);
    }

    void TS35DisplayDriver::sortSamples(std::uint16_t* samples, std::uint8_t count) {
        for (std::uint8_t index = 1; index < count; ++index) {
            const std::uint16_t value = samples[index];
            std::uint8_t        slot  = index;
            while (slot > 0 && samples[slot - 1] > value) {
                samples[slot] = samples[slot - 1];
                --slot;
            }
            samples[slot] = value;
        }
    }

    bool TS35DisplayDriver::readTouchRaw(std::uint16_t& rawX, std::uint16_t& rawY, std::uint16_t* pressure) {
        rawX = 0;
        rawY = 0;
        if (pressure != nullptr) {
            *pressure = 0;
        }
        if (!initialized_ || config_.touch.samples == 0 || config_.touch.samples > MaximumTouchSamples) {
            return false;
        }

        std::uint16_t      xSamples[MaximumTouchSamples];
        std::uint16_t      ySamples[MaximumTouchSamples];
        const std::uint8_t sampleCount = config_.touch.samples;

        beginTouchTransaction();
        const std::uint16_t pressureBefore = readPressureInTransaction();
        if (config_.touch.pressureThreshold != 0 && pressureBefore <= config_.touch.pressureThreshold) {
            endTouchTransaction();
            if (pressure != nullptr) {
                *pressure = pressureBefore;
            }
            return false;
        }

        for (std::uint8_t index = 0; index < sampleCount; ++index) {
            // The public MKS/TFT_eSPI driver clocks several conversions after each
            // mux change.  Discard one conversion here so the panel capacitance has
            // settled before the value enters the median filter.
            readAdc12InTransaction(TouchReadX);
            xSamples[index] = readAdc12InTransaction(TouchReadX);
            readAdc12InTransaction(TouchReadY);
            ySamples[index] = readAdc12InTransaction(TouchReadY);
        }
        const std::uint16_t pressureAfter = readPressureInTransaction();
        endTouchTransaction();

        const std::uint16_t measuredPressure = pressureBefore < pressureAfter ? pressureBefore : pressureAfter;
        if (pressure != nullptr) {
            *pressure = measuredPressure;
        }
        if (config_.touch.pressureThreshold != 0 && measuredPressure <= config_.touch.pressureThreshold) {
            return false;
        }

        sortSamples(xSamples, sampleCount);
        sortSamples(ySamples, sampleCount);
        const std::uint16_t xSpread = static_cast<std::uint16_t>(xSamples[sampleCount - 1] - xSamples[0]);
        const std::uint16_t ySpread = static_cast<std::uint16_t>(ySamples[sampleCount - 1] - ySamples[0]);
        if (config_.touch.maxSampleSpread != 0 && (xSpread > config_.touch.maxSampleSpread || ySpread > config_.touch.maxSampleSpread)) {
            return false;
        }

        rawX = xSamples[sampleCount / 2];
        rawY = ySamples[sampleCount / 2];
        return rawX != 0 && rawY != 0 && rawX != 0x0FFF && rawY != 0x0FFF;
    }

    std::int16_t TS35DisplayDriver::mapCalibrated(
        std::uint16_t raw, std::uint16_t minimum, std::uint16_t maximum, std::int16_t extent, bool invert) {
        std::int32_t clamped = raw;
        if (clamped < minimum) {
            clamped = minimum;
        } else if (clamped > maximum) {
            clamped = maximum;
        }

        const std::int32_t range  = static_cast<std::int32_t>(maximum) - minimum;
        std::int32_t       mapped = ((clamped - minimum) * (extent - 1) + range / 2) / range;
        if (invert) {
            mapped = (extent - 1) - mapped;
        }
        return static_cast<std::int16_t>(mapped);
    }

    bool TS35DisplayDriver::readTouch(std::int16_t& x, std::int16_t& y) {
        std::uint16_t rawX = 0;
        std::uint16_t rawY = 0;
        if (!readTouchRaw(rawX, rawY)) {
            return false;
        }

        const TouchCalibration& calibration = config_.touch;
        if (calibration.xMin >= calibration.xMax || calibration.yMin >= calibration.yMax) {
            return false;
        }

        const std::uint16_t logicalX = calibration.swapXY ? rawY : rawX;
        const std::uint16_t logicalY = calibration.swapXY ? rawX : rawY;
        x                            = mapCalibrated(logicalX, calibration.xMin, calibration.xMax, Width, calibration.invertX);
        y                            = mapCalibrated(logicalY, calibration.yMin, calibration.yMax, Height, calibration.invertY);
        return true;
    }

}  // namespace ts35
