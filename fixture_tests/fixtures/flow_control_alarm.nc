-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> o100 if [1 NE 1]
-> $Alarm/Send = 1
-> o100 else
-> (print, success)
<- ok
-> o100 endif
<- ok
<- ok
<- [MSG:INFO: PRINT, success]
<- ok
<- ok
