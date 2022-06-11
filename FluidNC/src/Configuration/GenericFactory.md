# How GenericFactory works

In short. Motors and spindles are registered through an abstract factory 
(GenericFactory.h). The 'factory' builds motors and spindle instances 
by name. The only thing you need to do is put the name in the header
 file (`const char* name() const override { return "null_motor"; }`) 
and put the registration in the cpp file 
`namespace { MotorFactory::InstanceBuilder<Nullmotor> registration("null_motor"); }`.
 In the yaml parser, the correct motor is then created in the handle 
method by calling `Motors::MotorFactory::factory(handler, _motor);`.

This means that there are literally no hard dependencies between motors. 
Not having dependencies is good, it can greatly help with compile-times 
and git merging. 

In long (How this works)... In C++, each template class is a separate 
type. So, that means that if we have a `MotorFactory` (Motors.h) and 
a `SpindleFactory` (Spindle.h), they don't share static members. Once 
registered, the builders in the factory returns the classes that can 
build a certain type; so a SpindleFactory won't have motors and visa 
versa. 

Registration works using a global variable. Global variables are 
instantiated before the first method is invoked. In our case this 
means it will call the constructor of the builder, which registers 
itself in the factory. There's just one more thing that needs to be 
solved at that point, and that's the fact that registration here will 
lead to make conflicts. This is solved by using an anonymous namespace,
 which ensures that these registration global variables are unique.

So, what happens... When the application starts up, it automatically 
runs the globals, which set up the name-to-builder mapping in the 
factory. Then, while parsing the yaml, the name is looked up, finds
a builder, and the instance is created.

# Further reading

GenericFactory is a C++ implementation of an 
[Abstract factory pattern](https://en.wikipedia.org/wiki/Abstract_factory_pattern).
