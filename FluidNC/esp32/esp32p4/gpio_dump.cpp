#include <Driver/gpio_dump.h>

#include <driver/gpio.h>
#include <soc/soc_caps.h>

void gpio_dump(Print& out) {
    out.println("GPIO dump:");

    for (int i = 0; i < SOC_GPIO_PIN_COUNT; ++i) {
        // Skip the USB pins.
        if (i >= 24 && i <= 27) {
            continue;
        }

        out.print("GPIO");
        out.print(i);
        out.print(" level=");
        out.println(gpio_get_level((gpio_num_t)i));
    }
}
