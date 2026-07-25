#include <Arduino.h>
#include <exception>

class TestException : public std::exception {
    const char* what() const noexcept override {
        return "Test exception";
    }
};

volatile bool exception_caught = false;
volatile bool exception_thrown = false;

void test_exception_main() {
    Serial.println("Test: Throwing exception from Arduino setup...");
    delay(100);
    
    try {
        Serial.println("  About to throw...");
        delay(100);
        exception_thrown = true;
        throw TestException();
        Serial.println("  ERROR: Code after throw executed!");
        delay(100);
    } catch (const TestException& e) {
        Serial.println("  SUCCESS: Caught TestException!");
        Serial.print("  what(): ");
        Serial.println(e.what());
        exception_caught = true;
        delay(100);
    } catch (...) {
        Serial.println("  SUCCESS: Caught unknown exception!");
        exception_caught = true;
        delay(100);
    }
    
    delay(1000);
    if (exception_thrown && exception_caught) {
        Serial.println("Test PASSED: Exceptions work!");
    } else {
        Serial.print("Test FAILED: thrown=");
        Serial.print(exception_thrown);
        Serial.print(", caught=");
        Serial.println(exception_caught);
    }
    delay(1000);
}

// Only compile this if explicitly requested
#ifdef TEST_EXCEPTIONS_ENABLED
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== Exception Test ===");
    test_exception_main();
}

void loop() {
    delay(1000);
}
#endif
