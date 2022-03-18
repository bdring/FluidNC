#include "StdTimer.h"
#include <iostream>


 
void tic_processor(StdTimer &pt, std::mutex &mt, int millisec)
{
    int tmp_rubs_for_move = 0;
    uint64_t cur_period = 0;

    for (;;)
    {
        //mt.lock();
        if (pt.IsStop())
        {
//            puts("stopped ticc\n");
         //   mt.unlock();
            break;
        }
        if (cur_period >= pt.get_timer_tic())
        {
            cur_period = 0;
            if (pt.is_enabled())
            {
                pt.do_action();
            }
        }
        cur_period++;
       // mt.unlock();
        std::this_thread::sleep_for(std::chrono::microseconds(millisec));
        
    } 
}


StdTimer::StdTimer(uint8_t timer, uint16_t divider, bool countUp) :
    _timer(timer), _divider(divider), _countUp(countUp), _action(0), _is_stop(true),
    _enable(false),_interrupt(0)
{
    /* Create the queue. */
}
StdTimer::~StdTimer()
{
    stop();
}

void StdTimer::start(uint32_t millisec)
{
    if (!_is_stop)
        return;
    _millisec = millisec;
    _is_stop = false;
    set_enable(false);
    std::thread(tic_processor, std::ref(*this), std::ref(_mt), millisec).detach();
}

void StdTimer::set_enable(bool enable)
{
   _enable = enable;
}

bool StdTimer::is_enabled() 
{ 
    return _enable;  
}

void StdTimer::set_action(void (*fn)(void)) 
{ 
    //std::lock_guard<std::mutex> lck (_mt);
    _action = fn; 
}
void StdTimer::set_pulse_tic(uint64_t interruptAt)
{
  //   std::lock_guard<std::mutex> lck (_mt);
    // interruptAt pulses in a second

   // _interrupt = 1000L/(interruptAt * _millisec);

   _interrupt = interruptAt/(_millisec*240);
   if (_countUp)
   {
       std::cout << "interrupt count: " << interruptAt << "\n";
   }
}


