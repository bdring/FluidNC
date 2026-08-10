// Copyright 2025 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <Driver/gpio_dump.h>
#include <Arduino.h>
#include <hardware/gpio.h>
#include <Platform.h>

// Map GPIO function codes to names
static const char* gpio_func_name(uint func) {
    switch (func) {
#ifdef PICO_RP2040
        case GPIO_FUNC_XIP:
            return "XIP";
#endif
        case GPIO_FUNC_SPI:
            return "SPI";
        case GPIO_FUNC_UART:
            return "UART";
        case GPIO_FUNC_I2C:
            return "I2C";
        case GPIO_FUNC_PWM:
            return "PWM";
        case GPIO_FUNC_SIO:
            return "SIO";
        case GPIO_FUNC_PIO0:
            return "PIO0";
        case GPIO_FUNC_PIO1:
            return "PIO1";
        case GPIO_FUNC_GPCK:
            return "GPCK";
        case GPIO_FUNC_USB:
            return "USB";
        default:
            return "?";
    }
}

void gpio_dump(Print& out) {
    out.println("GPIO Status:");
    const int max_gpio = MAX_N_GPIO;  // RP2350 has GPIO 0-47
    out.println("GPIO  Dir   Func   Level");
    out.println("----  ----  -----  -----");

    for (int pin = 0; pin < max_gpio; pin++) {
        // Get GPIO direction (true = output, false = input)
        bool is_output = gpio_get_dir(pin);

        // Get GPIO function
        uint        func     = gpio_get_function(pin);
        const char* func_str = gpio_func_name(func);

        // Get GPIO level
        bool level = gpio_get(pin);

        // Format output with padding
        if (pin < 10) {
            out.print("  ");
        } else {
            out.print(" ");
        }
        out.print(pin);
        out.print("    ");
        out.print(is_output ? "Out" : "In ");
        out.print("   ");
        out.print(func_str);
        out.print("     ");
        out.println(level ? "1" : "0");
    }

    out.println();
    out.println("Legend:");
    out.println("  Dir: In/Out (Input/Output)");
    out.println("  Func: GPIO function (SIO=GPIO, PWM=PWM, I2C=I2C, UART=UART, etc.)");
    out.println("  Level: Current pin level (0=Low, 1=High)");
    out.println();
#if 0
#    ifdef PICO_RP2040
    out.println("Note: GPIO 29 is reserved for QSPI flash interface");
#    else
    out.println("Note: GPIO 45-47 are dedicated functions (QSPI/Debug)");
#    endif
#endif
}
