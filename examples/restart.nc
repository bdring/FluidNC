(Restart macro -- resumes a job after a crash or power loss.)
(Delegated to from config.yaml via:)
(  macros:)
(    restart_macro: $SD/Run=restart.nc)
(Triggered by protocol_main_loop's dry-run-stop handling (Protocol.cpp) when a)
(check-mode dry run -- $C then $SD/Run=path or $LocalFS/Run=path, with a)
(matching $File/Breakpoint armed -- reaches its target line. gc_state has)
(been reconstructed by the dry run as of that line, so #<_target_x/y/z>,)
(#<_spindle_on/cw>, #<_rpm>, #<_flood>, and #<_mist> reflect where the file's)
(G-code would have left the tool and its spindle/coolant state. Coolant and)
(spindle are guaranteed off going into this, since a dry run never actuates)
(real hardware, so each action below only needs an on branch, never an off)
(branch.)
(This runs in normal, not single block, mode, so it can use these real)
(conditionals and its own explicit M0 pauses -- one immediately before each)
(action below, so nothing happens without the operator confirming it first.)

(PRINT, About to move XY to X#<_target_x> Y#<_target_y> -- cycle start to continue)
M0
G90
G0 X#<_target_x> Y#<_target_y>

o100 if [#<_spindle_on> EQ 1]
    o110 if [#<_spindle_cw> EQ 1]
        (PRINT, About to start spindle CW at S#<_rpm> -- cycle start to continue)
        M0
        M3 S#<_rpm>
    o110 else
        (PRINT, About to start spindle CCW at S#<_rpm> -- cycle start to continue)
        M0
        M4 S#<_rpm>
    o110 endif
o100 endif

o120 if [#<_flood> EQ 1]
    (PRINT, About to turn on flood coolant -- cycle start to continue)
    M0
    M8
o120 endif
o130 if [#<_mist> EQ 1]
    (PRINT, About to turn on mist coolant -- cycle start to continue)
    M0
    M7
o130 endif

(PRINT, About to plunge Z to #<_target_z> -- cycle start to continue)
M0
G1 Z#<_target_z> F50

o140 if [#<_incremental> EQ 1]
    G91
o140 endif
(PRINT, Restart sequence complete -- resuming job)
