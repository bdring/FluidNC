# Simple output stream

The simple output stream is a very basic implementation of 
`std::ostream`, but without all the plumbing and overhead 
of the std implementation, and without `dynamic_cast`.

The simple output stream is implemented to be able to stream
data, instead of allocating fixed buffers. For the YAML 
configuration system, this is mostly useful for streaming 
data to flash.

The syntax is the same as for std::ostream:

` myStream << "foo" << myInt << etc.;`

**NOTE:** Do NOT use `std::endl` or include `iostream`. Use basic 
types and strings like `"\n"` instead if needed. 

## Debug stream

`Logging` implements a debug stream, which is basically a way to 
do (development) logging without creating fixed-size buffers. 
Currently, the default implementation simply throws the log to
`Serial`, which should be probably improved at some point to 
use clients.

Usage:

```c++
log_info("Twelve is written as " << 12 << ", isn't it");
```

Available are the following macro's that default to 'void':

- log_debug
- log_info
- log_warn
- log_error
- log_fatal
