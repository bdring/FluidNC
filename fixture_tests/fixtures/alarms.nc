-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> $Alarm/Send=10
<- ok
<- [MSG:INFO: ALARM: Spindle Control]
# end in an unlocked state so other fixtures can run
-> $X
<- ALARM:10
<- [MSG:INFO: Caution: Unlocked]
<- ok
