; [ESP220]/spindle_testing.nc
G21
G90
G0X50
G0X100
M3 S10000 ; spindle on, spin up delay
G0X140
X60
X100
S15000 ; faster spindle up, no delay
G0X140
X60
X100
M5 ; spindle off, spin down delay
G0X0 ; return to 0
