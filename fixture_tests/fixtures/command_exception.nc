# A $ command that throws must answer with an error and leave command
# processing alive.  gpio.6 is reserved for the SPI flash on ESP32, so
# Pin::create() asserts.  Before the fix the exception escaped to loop(),
# which ended command processing, and the second one stalled the controller.
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> $GI=gpio.6
<~ [MSG:ERR: Command failed: Unusable GPIO]
<- error:3
-> $GI=gpio.6
<~ [MSG:ERR: Command failed: Unusable GPIO]
<- error:3
-> $GI=gpio.6
<~ [MSG:ERR: Command failed: Unusable GPIO]
<- error:3
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
