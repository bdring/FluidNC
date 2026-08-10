## Parameters/List

**Syntax:** `$PL`

Lists all global named parameters and local job parameters currently set in the system.

### Global Named Parameters
Named parameters that persist across job boundaries and are accessible from any context.

### Local Job Parameters
Parameters scoped to specific job execution levels. Each nesting level (main job and subroutines) maintains its own local parameter space. Useful for passing data between subroutines.

### Output Format

```
[MSG::INFO Named Parameters]
[MSG::INFO _param1 = 123.5]
[MSG::INFO _param2 = 456.78]
[MSG::INFO Job depth 0 - Local Parameters]
[MSG::INFO local_var = 10]
```

### Example

```gcode
#<my_param> = 100
$PL
```

Output:
```
[MSG::INFO Named Parameters]
[MSG::INFO _my_param = 100]
[MSG::INFO Job depth 0 - Local Parameters]
```

### Related Commands
- **Parameter Assignment** (`#<name> = value`) - Set parameter values in GCode
- **HttpCommand** (`$HTTP/COMMAND=...`) - Extract values into parameters from HTTP responses
