# How stepper works

The logic for the different stepping engines is better encapsulated but still distributed across several modules.  Timing - things like stepper disable delays, direction-to-step delay, step pulse length, and isr tick timing - used to be in Stepper.cpp with little fragments scattered throughout motion code.  Now the timing stuff has been collected in Stepping.cpp - mostly.  Step pulse generation still works like this: Stepper::pulse_func() determines the next step and calls Axes::step(step_mask, dir_mask).  If dir_mask has changed, Axes::step() loops over axes and motors and calls Motor::set_direction(bool) for each extant motor.  Axes::step() then loops over axes again and calls Motor::step() for each motor that is currently being driven.  ...

Motor::step() usually boils down to StandardStepper::step() via inheritance.

StandardStepper::step() looks at the stepping engine value.  If it is RMT, it starts a pulse on the RMT channel associated with that pin.  If not, it calls _step_pin.on(), vectoring to the PinDetail instance for the pin - either I2SOPinDetail or GPIOPinDetail.

GPIOPinDetail::on() calls __digitalWrite() after some error checking.

I2SOPinDetail::on() calls i2s_out_write() which is interesting.  In the streaming case, i2s_out_write sets or clears a bit in a bitmask variable, where it just sits until a later step.  In the passthrough (static) case, the bitmask variable is immediately sent to the output stream.

In I2SO streaming, the bitmask is not sent to the hardware until after all of the axes have been handled.  It happens in Stepping::waitPulse(), which call i2s_out_push_sample() to transfer the bitmask - which reflects the state of all of the step bits - to the DMA buffer.
