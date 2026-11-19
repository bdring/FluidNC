#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/spi_master.h>

namespace ts35 {

    // Minimal, synchronous driver for the passive MKS TS35/TS35-R panel.
    //
    // The panel has no processor of its own: an ST7796 LCD and an ADS7843E touch
    // controller sit on one SPI bus behind independent chip-select signals.  That
    // bus is the machine's shared one: Machine::SPIBus opens HSPI from the `spi:`
    // section before any module init() runs, and this driver only adds two devices
    // to it, exactly as the SD card does.  Every public drawing/touch operation
    // acquires the bus for its duration.  Calls must be made from normal task
    // context; none of these methods is safe to call from an ISR.
    class TS35DisplayDriver {
    public:
        enum : std::int16_t {
            Width  = 480,
            Height = 320,
        };

        // Native MCU GPIO numbers, resolved from FluidNC Pin objects by the caller.
        // -1 means "not wired", which only tftReset and backlight may be.
        struct Pins {
            std::int8_t tftCs     = -1;
            std::int8_t tftDc     = -1;
            std::int8_t tftReset  = -1;
            std::int8_t touchCs   = -1;
            std::int8_t backlight = -1;
        };

        struct TouchCalibration {
            // Bounds apply after swapXY.  In other words, xMin/xMax always describe
            // the raw channel that will become the landscape screen X coordinate.
            std::uint16_t xMin = 300;
            std::uint16_t xMax = 3600;
            std::uint16_t yMin = 300;
            std::uint16_t yMax = 3600;

            // XPT2046-style Z = 4095 + Z1 - Z2.  Set to zero only for controlled
            // diagnostics where pressure filtering is intentionally disabled.
            std::uint16_t pressureThreshold = 350;
            std::uint16_t maxSampleSpread   = 40;
            std::uint8_t  samples           = 5;

            bool swapXY  = true;
            bool invertX = true;
            bool invertY = false;
        };

        struct Config {
            Pins             pins;
            TouchCalibration touch;

            std::uint32_t lcdClockHz   = 40000000U;
            std::uint32_t touchClockHz = 2000000U;

            // Public MKS sources disagree across board revisions about GPIO5
            // polarity, so this is deliberately a runtime setting.
            bool backlightActiveLow = true;
            bool invertDisplay      = false;
        };

        TS35DisplayDriver();
        explicit TS35DisplayDriver(const Config& config);
        ~TS35DisplayDriver();

        TS35DisplayDriver(const TS35DisplayDriver&)            = delete;
        TS35DisplayDriver& operator=(const TS35DisplayDriver&) = delete;

        // Configure before begin().  Any devices from a previous begin() are
        // removed, so pins and controller state cannot diverge from the new
        // settings.
        void setConfig(const Config& config);

        // Attaches the LCD and the touch controller to the already-open shared SPI
        // bus, hardware-resets the panel, runs the MKS ST7796 sequence, clears the
        // screen and lights the backlight.  false means the bus was not open or
        // the configuration was rejected; true only means the sequence was issued,
        // since the TS35 MISO wiring cannot provide a reliable controller-ID probe.
        bool begin();

        // Detaches both devices from the shared bus and turns the backlight off.
        void end();

        void setBacklight(bool on);

        void fillScreen(std::uint16_t color);
        void fillRect(std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height, std::uint16_t color);
        void drawRect(std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height, std::uint16_t color);

        // Paints the complete 6x8 character cell (5 columns plus spacing) using
        // the supplied background, so a redraw covers whatever was there before.
        // scale zero is ignored.
        void drawText(std::int16_t x, std::int16_t y, const char* text, std::uint16_t foreground, std::uint16_t background, std::uint8_t scale);

        // Returns physical ADC channels before swap/inversion/calibration.  rawX
        // is command 0xD0 and rawY is command 0x90.  pressure may be null.
        bool readTouchRaw(std::uint16_t& rawX, std::uint16_t& rawY, std::uint16_t* pressure = nullptr);

        // Returns calibrated landscape coordinates in [0,479] x [0,319].
        bool readTouch(std::int16_t& x, std::int16_t& y);

    private:
        Config              config_;
        spi_device_handle_t lcd_;
        spi_device_handle_t touch_;
        bool                initialized_;

        bool validConfig() const;
        bool addDevices();

        void beginLcdTransaction();
        void endLcdTransaction();
        void beginTouchTransaction();
        void endTouchTransaction();

        static void writeBytes(spi_device_handle_t device, const std::uint8_t* data, std::size_t length);

        void writeCommand(std::uint8_t command, const std::uint8_t* data = nullptr, std::uint8_t length = 0);
        void writeCommandInTransaction(std::uint8_t command, const std::uint8_t* data = nullptr, std::uint8_t length = 0);
        void setAddressWindowInTransaction(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height);
        void writeSolidPixelsInTransaction(std::uint16_t color, std::uint32_t count);
        void fillRectInTransaction(std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height, std::uint16_t color);
        void drawGlyphInTransaction(
            std::int16_t x, std::int16_t y, char character, std::uint16_t foreground, std::uint16_t background, std::uint8_t scale);

        bool clipRect(std::int16_t& x, std::int16_t& y, std::int16_t& width, std::int16_t& height) const;

        std::uint16_t readAdc12InTransaction(std::uint8_t command);
        std::uint16_t readPressureInTransaction();
        static void   sortSamples(std::uint16_t* samples, std::uint8_t count);
        static std::int16_t mapCalibrated(std::uint16_t raw, std::uint16_t minimum, std::uint16_t maximum, std::int16_t extent, bool invert);
    };

}  // namespace ts35
