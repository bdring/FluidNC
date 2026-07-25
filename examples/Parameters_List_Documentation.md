# Parameters/List Command Documentation

## Overview

The **Parameters/List** command (`$PL`) lists all parameters currently available in FluidNC, including both globally-scoped named parameters and locally-scoped job parameters. This is useful for debugging, monitoring parameter state, and understanding which parameters are set at different job nesting levels.

## Command Reference

### Syntax

```gcode
$PL
```

### Short Code
- **`PL`** - Parameters/List

## Description

The Parameters/List command enumerates all parameters in the system and displays them grouped by scope:

1. **Global Named Parameters** - Parameters defined at the global scope that persist across job boundaries
2. **Local Job Parameters** - Parameters scoped to specific job execution levels, including parameters set in nested jobs (subroutines)

### Parameter Scopes

**Global Named Parameters:**
- Persistent throughout FluidNC execution
- Accessible from any job context
- Set via GCode parameter assignment (e.g., `#<param_name> = 100`)
- Can be passed between GCode programs

**Local Job Parameters:**
- Scoped to specific job execution contexts
- Each job nesting level has its own local parameter space
- Useful for subroutines and nested GCode programs
- Automatically cleaned up when the job context exits

## Output Format

The command produces formatted output listing all parameters:

```
[Named Parameters]
some_param = 123.5
another_param = 456.78

[Job depth 0 - Local Parameters]
local_var = 10
temp_reading = 25.5

[Job depth 1 - Local Parameters]
loop_counter = 5
status_flag = 1
```

When no parameters are set:

```
[No named parameters defined]
[No active jobs - no local parameters]
```

When a specific job level has no parameters:

```
[Job depth 1 - No local parameters]
```

## Examples

### Basic Parameter Listing

View all current parameters:
```gcode
$PL
```

Output:
```
[Named Parameters]
feed_rate = 100
spindle_speed = 5000

[Job depth 0 - Local Parameters]
cut_depth = 3.5
```

### In a Subroutine

When called from within a nested job (subroutine), shows parameters at all nesting levels:

```gcode
#<my_global> = 42
o100 sub
  #<my_local> = 99
  $PL
o100 endsub
o100 call
```

Output shows both global and local parameters at each depth:
```
[Named Parameters]
my_global = 42

[Job depth 0 - Local Parameters]
(none listed if no local params at depth 0)

[Job depth 1 - Local Parameters]
my_local = 99
```

### Monitoring Parameter State During Execution

Use in diagnostic macros to verify parameter values:

```gcode
; Set some initial parameters
#<tool_number> = 1
#<offset_x> = 10.5
#<offset_y> = 20.5

; Check current state
$PL

; Run job
G00 X100 Y100

; Check state after job
$PL
```

## Related Features

- **Parameter Assignment** - Set parameters using GCode: `#<param_name> = value`
- **Parameter Evaluation** - Use parameters in GCode expressions: `G00 X[#<param_name> + 5]`
- **HttpCommand with Parameters** - Extract HTTP response values into parameters for use in subsequent GCode:
  ```gcode
  $HTTP/COMMAND=http://sensor:8000/api/temp{"extract":{"_temp":"temperature"}}
  ; Temperature now available in #<_temp>
  ```
- **Job Context Management** - Parameters are scoped to job execution contexts for modular GCode programs

## Access via HTTP API

The Parameters/List functionality can be accessed via HTTP GET request:

```
GET /api/commands/pl
```

Returns the same parameter listing output that would be displayed on the serial console.

## Use Cases

1. **Debugging** - Verify parameter values during GCode execution
2. **Monitoring** - Track parameter changes throughout a job sequence
3. **Diagnostics** - Understand parameter scoping in nested jobs
4. **Automation** - Programmatically check parameter state via HTTP API
5. **Data Validation** - Confirm that parameters extracted from HTTP responses are correctly set

## Notes

- Parameters are floating-point values; GCode parameter names typically use `#<name>` syntax
- Global named parameters persist until explicitly cleared or FluidNC restarts
- Local parameters are automatically cleared when their job context exits
- The command requires appropriate authentication level (by default available in all states)
- Output is displayed on the current output channel (serial, HTTP, etc.)

## See Also

- [GCode Parameters](https://wiki.fluidnc.com/en/features/gcode_parameters)
- [HttpCommand Feature](https://wiki.fluidnc.com/en/features/http_command)
- [Job Execution Model](https://wiki.fluidnc.com/en/features/job_execution)
