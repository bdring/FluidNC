#include "ledc.h"
#include "../soc/ledc_struct.h"
#include "../Arduino.h"
#include "../Capture.h"

#include <sstream>

class LEDCInfo {
public:
    LEDCInfo() : chan_(0), freq_(0), pin_(0) {}
    LEDCInfo(int chan, double freq, int pin) : chan_(chan), freq_(freq), pin_(pin) {
        std::ostringstream oss;
        oss << "ledc." << chan << ";freq=" << freq << ";pin=" << pin;
        key_ = oss.str();
    }

    int         chan_;
    double      freq_;
    int         pin_;
    std::string key_;

    void write(uint32_t duty) { Capture::instance().write(key_, duty); }
};

LEDCInfo infos[16];

uint32_t getApbFrequency() {  // in Hz
    return 80'000'000;        // 80 MHz
}
double ledcSetup(uint8_t chan, double freq, uint8_t resolution_bits) {
    infos[chan] = LEDCInfo(chan, freq, 0);
    return freq;
}
void ledcWrite(uint8_t channel, uint32_t duty) {
    infos[channel].write(duty);
}
void ledcAttachPin(uint8_t pin, uint8_t channel) {
    auto old       = infos[channel];
    infos[channel] = LEDCInfo(channel, old.freq_, pin);
}
void ledcDetachPin(uint8_t pin) {
    for (int i = 0; i < 16; ++i) {
        if (infos[i].pin_ == pin) {
            infos[i] = LEDCInfo(0, 0, 0);
        }
    }
}

// TODO FIXME: Find some way to cope with these:
void pinMatrixOutAttach(uint8_t pin, uint8_t function, bool invertOut, bool invertEnable) {}  // Cannot capture this...
void pinMatrixOutDetach(uint8_t pin, bool invertOut, bool invertEnable) {}                    // Cannot capture this...
