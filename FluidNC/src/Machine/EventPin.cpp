#include "EventPin.h"

#include "src/Report.h"    // addPinReport
#include "src/Protocol.h"  // event_queue
#include "src/System.h"    // sys
#include <esp_attr.h>      // IRAM_ATTR

#include "soc/soc.h"
#include "soc/gpio_periph.h"
#include "hal/gpio_hal.h"

namespace Machine {
    std::list<EventPin*> EventPin::_blockedPins;

    TimerHandle_t EventPin::_eventPinTimer = 0;

    EventPin::EventPin(Event* event, const char* legend) : _event(event), _legend(legend) {
        if (_eventPinTimer == 0) {
            _eventPinTimer = xTimerCreate("eventPinTimer", pdMS_TO_TICKS(200), false, NULL, eventPinTimerCallback);
        }
    }
    EventPin::EventPin(Event* event, const char* legend, Pin& pin) : EventPin(event, legend) { _pin.swap(pin); }
    bool EventPin::get() { return _pin.read(); }
    void EventPin::eventPinTimerCallback(void*) { check(); }
    void EventPin::run(void* arg) {
        // Since we do not trust the ISR to always trigger precisely,
        // we check the pin state before calling the event handler
        if (get()) {
            block();
            _event->run(arg);
        } else {
            reArm();
        }
    }
    void EventPin::check() { _blockedPins.remove_if(inactive); }

    void EventPin::block() { _blockedPins.emplace_back(this); }

    bool EventPin::inactive(EventPin* pin) {
        bool value = pin->get();

        pin->update(value);
        if (value) {
            if (sys.state != State::Homing) {
                xTimerStart(_eventPinTimer, 0);
            }
        } else {
            pin->reArm();
        }
        return !value;
    }

    void IRAM_ATTR EventPin::handleISR() {
        // This is the body of gpio_hal_intr_disable() which is not IRAM_ATTR
        gpio_num_t  gpio_num = gpio_num_t(_gpio);
        gpio_dev_t* dev      = GPIO_LL_GET_HW(GPIO_PORT_0);
        gpio_ll_intr_disable(dev, gpio_num);
        if (gpio_num < 32) {
            gpio_ll_clear_intr_status(dev, BIT(gpio_num));
        } else {
            gpio_ll_clear_intr_status_high(dev, BIT(gpio_num - 32));
        }
        protocol_send_event_from_ISR(this, this);
    }
    void EventPin::startTimer() { xTimerStart(_eventPinTimer, 0); }

    void EventPin::reArm() { gpio_intr_enable(gpio_num_t(_gpio)); }
    void EventPin::init() {
        if (_pin.undefined()) {
            return;
        }

        _pin.report(_legend);
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (_pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        _pin.setAttr(attr);
        _pin.attachInterrupt(ISRHandler, Pin::EITHER_EDGE, this);
        _gpio = _pin.getNative(Pin::Capabilities::Input | Pin::Capabilities::ISR);
    }

    EventPin::~EventPin() { _pin.detachInterrupt(); }
};
