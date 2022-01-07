#pragma once

/* Kernel includes. */
#include <freertos\FreeRTOS.h>
#include <thread>
#include <concurrent_queue.h>
#include <iostream>

//typedef Concurrency::concurrent_queue<uint8_t> WorkQueue;
typedef std::queue<uint8_t> WorkQueue;

struct hw_timer_s {
    virtual ~hw_timer_s() {} 
};
class StdTimer;
void tic_processor(StdTimer &pt, std::mutex &mt, int millisec);

class StdTimer : public hw_timer_s
{
public:
    StdTimer(uint8_t timer, uint16_t divider, bool countUp);
    ~StdTimer();
    void start(uint32_t millisec);
    void set_enable( bool enable);
    bool is_enabled();
    void set_pulse_tic(uint64_t interruptAt);

    bool IsStop() { return _is_stop; }
    uint8_t getTimerId() { return _timer; }
    void set_action(void(*fn)(void));
    void stop() { _is_stop = true; }
    WorkQueue &GetQueue() { return _queue;  }
    uint64_t get_timer_tic() { return _interrupt; }
    void do_action() 
    { 
        static int pulse_count = 0;
        if (_countUp)
        {
            std::cout << pulse_count << " puls\n";
            pulse_count++;
        }

        if (!_action) return;
       _action(); 
    }

public:
    bool _is_stop;
private:
    std::mutex _mt;
    uint8_t _timer;
    bool _enable;
    uint16_t _divider;
    bool _countUp;
    void (*_action)(void);
    uint64_t _interrupt;
    WorkQueue _queue;
    int _millisec;
  
};

