#pragma once

typedef enum : unsigned char {
    URL_CALL_OK = 1,
    NO_URL,
    NO_GOOD_MODE,
    NOT_SUCCESSFUL,
} urlFeedback;

urlFeedback CallURL(String cmd);
void        CallURLWithRetryStrategy(String cmd);
String      GetCMDEndPrg();
String      GetCMDStartPrg();