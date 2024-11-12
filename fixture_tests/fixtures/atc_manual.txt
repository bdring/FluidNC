; ATC Manual Test Procedure
; Create a config with 2 spindles. First spindle has atc: atc_manual. Second spindle tool_num: 10
; Not compatible with the test fixture program yet.
; single spindle testing
$BYE ; hard reset
$H ; home
$G ; we should see T0
M61 Q1 ; Set tool 1. Nothing should move
$G ; tool should be T1
G0 X0 Y0 ; move to work zero
G38.2 G91 Z-150 F100 P0 ; probe with tool 1 and set z zero
G53 G0 Z-10 ; move up
M3 S12000 ; spindle should turn on
G1 X50 F100; A slow G1 move. Send the next command as soon as possible. Spindle should not stop until move is complete.
M6T2 ; Spindle off, measure current tool, ask for next tool, touch toolsetter, return to work, spindle on
$# ; you should see a TLO value
M6T3 ; Spindle off, Move to change location, ask for new tool, measure it, return to work, spindle on
M61 Q0
$# ; Spindle off, TLO should set to zero
$G ; Tool should be 0 speed should be 12000

; multi spindle testing
M3 ; Spindle on
M6T10 ; Spindle off, spindle changes; 
$G ; Tool 10 and S0
M3S12000; New spindle is on.
