# Per-command jog feed override: syntax, opt-in and unchanged modal state.
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> G21 G90 G94 F123
<- ok
# A zero-distance capability probe, with explicit relative positioning.
-> $J=G21 G91 X0 F1 P1
<- ok
-> $J=G21 G91 X0 F1
<- ok
-> $J=G21 G91 X0 F1 P0
<- ok
-> $G
<- [GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F123 S0]
<- ok
# Invalid flags must reject the entire jog.
-> $J=G21 G91 X1 F100 P2
<- error:16
<~ [MSG:ERR: Invalid jog command]
-> $J=G21 G91 X1 F100 P0.5
<- error:16
<~ [MSG:ERR: Invalid jog command]
-> $J=G21 G91 X1 F100 P-1
<- error:4
<~ [MSG:ERR: Negative value]
-> $J=G21 G91 X1 F100 P1 P1
<- error:25
<~ [MSG:ERR: Gcode word repeated]
# P1 does not relax the required feed word or permitted jog G-codes.
-> $J=G21 G91 X1 P1
<- error:22
<~ [MSG:ERR: Gcode undefined feed rate]
-> $J=G1 G91 X1 F100 P1
<- error:16
<~ [MSG:ERR: Invalid jog command]
# P remains an unused word on ordinary G1 moves.
-> G1 X1 F100 P1
<- [MSG:ERR: Bad GCode: G1 X1 F100 P1]
<- error:36
<~ [MSG:ERR: Gcode unused words]
