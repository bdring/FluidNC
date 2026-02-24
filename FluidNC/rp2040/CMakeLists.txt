# CMakeLists.txt for RP2040-specific components

add_library(rp2040_drivers
    StepTimer.cpp
    gpio.cpp
    uart.cpp
    spi.cpp
    wdt.cpp
    delay_usecs.cpp
    littlefs.cpp
    nvs.cpp
    GPIOCapabilities.cpp
    VFS.cpp
)

target_include_directories(rp2040_drivers PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PICO_SDK_PATH}/src/common/pico_stdlib/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_uart/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_spi/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_timer/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_gpio/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_watchdog/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_irq/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_flash/include
)

target_link_libraries(rp2040_drivers
    pico_stdlib
    hardware_uart
    hardware_spi
    hardware_timer
    hardware_gpio
    hardware_watchdog
    hardware_irq
    hardware_flash
)
