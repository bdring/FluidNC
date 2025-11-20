idf_component_register(SRCS AssertionFailed.cpp
                            delay_usecs.cpp
                            esp_error.cpp
                            gpio.cpp
                            gpio_dump.cpp
                            i2c.cpp
                            littlefs.cpp
                            localfs.cpp
                            nvs.cpp
                            PwmPin.cpp
                            restart.cpp
                            sdspi.cpp
                            spi.cpp
                            spiffs.cpp
                            StartupLog.cpp
                            StepTimer.cpp
                            tmc_spi.cpp
                            fnc_uart.cpp
                            wdt.cpp
                            coredump.c
                            fnc_idf_uart.c
                            timed_engine.c
                            tmc_spi_support.c
                            # esp32/Console.cpp
                            # esp32/GPIOCapabilities.cpp
                            # esp32/i2s_engine.c
                            # esp32/rmt_engine.c
                            # esp32s2/GPIOCapabilities.cpp
                            esp32s3/Console.cpp
                            esp32s3/GPIOCapabilities.cpp
                            esp32s3/USBCDCChannel_IDF.cpp
                            esp32s3/i2s_engine_dedicated.c
                       INCLUDE_DIRS "." ".." "../include" "../lib/TMCStepper/src" "esp32s3" "../src"
                       REQUIRES esp_driver_uart driver fatfs vfs spiffs esp_driver_sdspi esp_timer arduino-esp32 nvs_flash esp_driver_gpio mbedtls esp_tinyusb
                       PRIV_REQUIRES espcoredump
                       WHOLE_ARCHIVE)
