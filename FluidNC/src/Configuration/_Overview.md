# Overview 

Users who are just interested in adding new settings, should continue 
reading the sections below. 

The configuration system here is based on yaml-ish settings that are 
parsed by a [tokenizer/parser](Parser.md) combination and 
[configuration handlers](ConfigurationHandlers.md) which add 
functionality like tree building and validation.

It is worth noting that some parts of the configuration system might 
seem strange at first, due to the way it works. The main reason of 
implementing it like this, is because no RTTI (e.g. `dynamic_cast`)
is available on the ESP32, which severily limits the possibities in 
the architecture.

Because of that, we basically iterate all settings within a section 
for all yaml settings that we encounter. Normally that would seem like 
a lot of overhead - but because the number of settings within a section
is small, and an ESP32 is quite powerful, the end result is still 
more than fast enough for normal operations.

## Implementing a configurable

Most users of the configuration system only need to implement a 
`Configurable` to get access to the configuration system. The 
*Machine* folder is the main place where configurables that only 
define structure reside (e.g. axis, etc). 

There are basically 2 types of configurable.

The first type are basic structural classes, with a certain 
functionality. For example, `I2SO` or `Endstops`. These are 
very simple classes, that only hold a few fields (the things 
you want to configure), a constructor (usually `= default`), 
some validation, and a `handle` method that maps the settings 
to fields. For example:

```c++

class I2SOBus : public Configuration::Configurable {
public:
    I2SOBus() = default;

    Pin _bck;
    // ...

    void validate() const override {
        if (!_bck.undefined() || !_data.undefined() || !_ws.undefined()) {
            Assert(!_bck.undefined(), "I2SO BCK pin should be configured once.");
            // ...
        }
    }

    void group(Configuration::HandlerBase& handler) {
        handler.item("bck", _bck);
        // ...
    }

    ~I2SOBus() = default;
};
```

The second type are collections with multiple derived types and 
a common access point from the class hierarchy. Good examples are 
Motors and Spindles. 

These classes implement Configurable (the same way as above), but 
with the notable difference that the classes are built by a factory,
which defines the mapping of the name to the instance (e.g. from 
the string "standard_stepper" in the config to the StandardStepper
class).

Registration _must_ happen in the C++ file, in the local namespace
and the syntax _must_ be as follows:

    // Configuration registration
    namespace
    {
        MotorFactory::InstanceBuilder<StandardStepper> registration("standard_stepper");
    }

There are three C++ tricks used in this piece of code:

First off, an instance builder class is instantiated as global 
object. The template argument here is assumed to have a default 
constructor, which is used to instantiate the object. The registration
object itself is instantiated during startup of the application, 
but the StandardStepper is built only when the factory is used.

Second, notice `MotorFactory`. Each instance builder only builds 
instances of a certain base class, in this case `Motor`. MotorFactory
is in this case just a one-liner, defined in the header file of the 
base class `Motor`, namely:

`using MotorFactory = Configuration::GenericFactory<Motor>;`

This factory-per-baseclass is necessary to map the individual 
instances to a shared baseclass. And because `StandardStepper` 
derives from `Motor`, the `MotorFactory` should be used.

Last, the `namespace` here is an anonymous namespace. If we would
not use an anonymous namespace, the registration instances would 
trigger a link-time name collision. 

## Getting started with Yaml settings

During startup, `MachineConfig::load` is called, with the 
default filename `/localfs/config.yaml`.

You can upload a new config file to localfs by putting it in the
data folder, and calling `pio run -t uploadfs`. Another option
is to upload it using WiFi.

After uploading, just reboot the ESP32 and have fun.
