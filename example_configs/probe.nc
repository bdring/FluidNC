#<probe_z>=#5063
#<start_x>=#<_x>
#<start_y>=#<_y>
#<start_z>=#<_z>
g53G0z-1 ; move to top of Z
M5
G53G0X5Y-9 ; move over toolsetter
G53G0Z#</atc_manual/ets_rapid_z_mpos_mm>  ; rapid down
G53G38.2Z-40F500
G0Z[#<_z> + 5]
G53G38.2Z-40F80
#<_ets_tool1_z>=[#5063]
D#<_ets_tool1_z>
g53G0z-1 ; move to top of Z
G53G0X80Y0Z-1  ;move to change location
G4P0.1 ; sync
(MSG: Install tool #1 then resume to continue)
M0 ; pause
G53G0X5Y-9 ; move over toolsetter
G53G0Z#</atc_manual/ets_rapid_z_mpos_mm>  ; rapid down
G53G38.2Z-40F500
G0Z[#<_z> + 5]
G53G38.2Z-40F80
#<_my_tlo_z>=[#5063-#<_ets_tool1_z>]
G43.1Z#<_my_tlo_z>
g53G0z-1 ; move to top of Z
G0X#<start_x>Y#<start_y>
M3

