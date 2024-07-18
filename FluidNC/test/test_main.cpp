#ifdef ESP32

#    include "TestFactory.h"
#    include <Arduino.h>
#    include <HardwareSerial.h>
#    include "unity.h"

void test_blank() {
    int i = 5;
    TEST_ASSERT_EQUAL(i, 5);
}

void setup() {
    delay_ms(500);  // Let's give it some time first, in case it triggers a reboot.

    UNITY_BEGIN();

    // calls to tests will go here
    // RUN_TEST(test_blank);

    // Run all tests:
    TestFactory::instance().runAll();

    UNITY_END();  // stop unit testing
}

void loop() {}

#endif
