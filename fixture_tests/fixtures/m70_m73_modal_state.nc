# M70/M71/M72/M73 modal state save/restore
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> $G
<- [GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]
<- ok
# M72 with nothing saved is an error
-> M72
<- [MSG:ERR: Bad GCode: M72]
<- error:182
<~ [MSG:ERR: Gcode M72 without saved modal state]
# M70 then M72 restores the saved state
-> G18 G91 G55 G93 F300 S100
<- ok
-> M70
<- ok
-> G17 G90 G56 G94 F500 S200
<- ok
-> $G
<- [GC:G0 G56 G17 G21 G90 G94 M5 M9 T0 F500 S200]
<- ok
-> M72
<- ok
-> $G
<- [GC:G0 G55 G18 G21 G91 G93 M5 M9 T0 F300 S100]
<- ok
# Motion mode is not saved or restored
-> G1 F100
<- ok
-> M70
<- ok
-> G0
<- ok
-> M72
<- ok
-> $G
<- [GC:G0 G55 G18 G21 G91 G93 M5 M9 T0 F100 S100]
<- ok
# Restoring again works; the saved state is not consumed
-> G17 G90 G54 G94
<- ok
-> M72
<- ok
-> $G
<- [GC:G0 G55 G18 G21 G91 G93 M5 M9 T0 F100 S100]
<- ok
# M71 invalidates the saved state
-> M71
<- ok
-> M72
<- [MSG:ERR: Bad GCode: M72]
<- error:182
<~ [MSG:ERR: Gcode M72 without saved modal state]
# M70-M73 must be alone in the block, apart from N
-> M70 G0
<- [MSG:ERR: Bad GCode: M70 G0]
<- error:21
<~ [MSG:ERR: Gcode modal group violation]
-> M72 X1
<- [MSG:ERR: Bad GCode: M72 X1]
<- error:36
<~ [MSG:ERR: Gcode unused words]
-> N10 M70
<- ok
-> $J=M70 X1 F100
<- error:16
<~ [MSG:ERR: Invalid jog command]
# M73 in a job (here a macro) restores the state when that job ends.
# Macro0 runs Macro1 as a nested job and reports the state after Macro1
# is popped, so the report cannot race the end-of-job restore.
-> G17 G90 G54 G94 F500 S100
<- ok
-> $Macros/Macro1=G18 F200&M73&G19 G91 G59 F7&$G
<- ok
-> $Macros/Macro0=$Macros/Run=1&$G
<- ok
-> $Macros/Run=0
<- ok
<- [GC:G0 G59 G19 G21 G91 G94 M5 M9 T0 F7 S100]
<- [GC:G0 G54 G18 G21 G90 G94 M5 M9 T0 F200 S100]
# The job's M73 did not overwrite the base level's state from N10 M70
-> M72
<- ok
-> $G
<- [GC:G0 G55 G18 G21 G91 G93 M5 M9 T0 F100 S100]
<- ok
# M2 ends the job without the M73 restore
-> G17 G94 F100
<- ok
-> $Macros/Macro1=G18&M73&G19&M2
<- ok
-> $Macros/Run=0
<- ok
<- [MSG:INFO: Program End]
<- [GC:G1 G54 G17 G21 G90 G94 M5 M9 T0 F100 S100]
