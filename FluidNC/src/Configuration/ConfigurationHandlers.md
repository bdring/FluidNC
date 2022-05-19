# Configuration handlers

The main access point for all configuration related things is a 
configuration handler, implemented in `HandlerBase.h`. This 
is an interface class, and serves to provide a single access 
point for:

- Parsing yaml files
- Fixup operations
- Runtime (ad-hoc) configuration settings
- Legacy ($nnn) configuration settings
- Configuration validation
- Yaml generation

## Normal operations

Classes that can be configured, implement the `Configurable` 
base class and defines some anchor somewhere in the machine 
configuration tree.

The constructor of a configurable class should have the same behavior
as a `test_drive` machine, e.g. a void machine that just works, but 
does nothing otherwise.

The different configuration handlers then call these methods for 
the settings, if they are present.

Note that configuration handlers will be called multiple times. In 
fact, for each setting in a yaml section, the `handle` call will be 
invoked. The handler itself then just maps the settings to fields.
For example:

```c++

class I2SOBus : public Configuration::Configurable {
public:
    I2SOBus() = default;

    Pin _bck;
    Pin _data;
    Pin _ws;

    void validate() const override {
        if (!_bck.undefined() || !_data.undefined() || !_ws.undefined()) {
            Assert(!_bck.undefined(), "I2SO BCK pin should be configured once.");
            Assert(!_data.undefined(), "I2SO Data pin should be configured once.");
            Assert(!_ws.undefined(), "I2SO WS pin should be configured once.");
        }
        // more validations?
    }

    void group(Configuration::HandlerBase& handler) {
        handler.item("bck", _bck);
        handler.item("data", _data);
        handler.item("ws", _ws);
    }

    ~I2SOBus() = default;
};
```

## Parser handler

What normally happens, is that for all the configuration settings
within the relevant section, the `handle` call of the Configurable 
is called. The handler has a callback with some convenience methods:

`handler.item("bck", _bck);` -- this maps class field `_bck` on
the setting named "bck". Note that the default setting is normally 
defined in the constructor, or if that's not sufficient, in the 
`afterParse` method.

The currently supported types are:

- bool
- int
- float
- Pin
- String

While `handle` might suggest that this is where (only) yaml parsing 
occurs, this is not the case. `handle` is called by all the configuration
handlers, to handle all the relevant parts of the tree. So in a sense, 
`handle` defines how the tree is mapped from yaml to classes.

## Validation handler

The validation handler traverses the tree (by using `handle`) and 
calls all `validate` methods on it. Validation here should mainly 
focus on using assertions with some (user) useful description on 
what is wrong or what should happen.

## After parse

In some rare cases, setting up the defaults in the constructor is just 
not going to work, and we need to do some fix-up after parsing is 
complete. The after-parse handler does just this, by calling the 
`afterParse` methods in all the Configurable classes. The default 
behavior of this is not-implemented, which does nothing (and therefore
assumes that everything is setup in the constructor).

## Runtime settings

Like yaml settings, the runtime settings basically traverse the 
tree by calling `handle` methods, and then picks only the nodes that 
match the runtime setting. 

In the current state, this supports settings of the form 
`$coolant/flood=gpio.2:pu`. Note the '/' that separates sections 
(coolant being the only one here), and the '=' that defines the 
key/value boundary. 

## Legacy setting handler

In old GRBL style, legacy settings like `$100=333` were used to define
things like feed rates, acceleration, etc. The legacy settings handler
makes it possible to support such settings, by mapping the legacy 
settings to one (or more) paths in the yaml tree. 

At this point, this is simply not implemented. 

## Generator

The generator basically traverses the complete tree, and generates a 
yaml file. The main point of this is to create a `config.yaml` file 
in LocalFS, which holds the main startup configuration.
