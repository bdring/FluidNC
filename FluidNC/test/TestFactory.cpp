#include "TestFactory.h"

#include <src/Assertion.h>
#include <WString.h>

#ifdef ESP32

#    include "unity.h"
#    include <cstdio>

void TestFactory::runAll() {
    int  i       = 0;
    auto current = first;
    while (current) {
        ++i;
        auto curTestName = current->unitTestName();
        auto curTestCase = current->unitTestCase();

        char fullName[80];
        snprintf(fullName, 80, "%s:%s", curTestName, curTestCase);

        auto function = current->getFunction();
        UnityDefaultTestRun(function, fullName, i);

        current = current->next;
    }
}

#else

#    include <iostream>
#    include <cstring>
#    include <cstdio>

#    if defined _WIN32 || defined _WIN64
#        define WIN32_LEAN_AND_MEAN
#        include <Windows.h>
void setColor(int colorIndex) {  // 10 = green, 12 = red, 7 = gray, 15 = white
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    // you can loop k higher to see more color choices
    // pick the colorattribute k you want
    SetConsoleTextAttribute(hConsole, colorIndex);
}
#    else
void setColor(int colorIndex) {}
#    endif

void TestFactory::runAll() {
    const int Indent = 80;

    char spaces[Indent];
    memset(spaces, ' ', Indent - 1);
    spaces[Indent - 1] = '\0';

    auto        current = first;
    const char* prev    = nullptr;
    while (current) {
        auto curTest = current->unitTestName();

        setColor(15);

        if (prev == nullptr || !strcmp(prev, curTest)) {
            printf("- Test: %s\r\n", curTest);
            prev = curTest;
        }

        setColor(7);

        printf("  - Case: %s", current->unitTestCase());

        int len = int(strlen(current->unitTestCase()));
        if (len >= (Indent - 5)) {
            len = (Indent - 5);
        }
        printf(spaces + len);  // pad.

        try {
            setColor(10);

            current->run();
            printf("Passed.\r\n");
        } catch (std::exception& ex) {
            setColor(12);
            printf("FAILED!\r\n");
            printf("\r\n");
        } catch (...) {
            setColor(12);
            printf("FAILED!\r\n");
            // We don't know where unfortunately...
        }
        current = current->next;

        setColor(7);
    }

    printf("\r\nDone.\r\n");
}

#endif
