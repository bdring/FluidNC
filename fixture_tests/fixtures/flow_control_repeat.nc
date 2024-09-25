ignore ok

# test repeat zero times (should not print anything)
-> o100 repeat [0]
-> (print, fail lit 0)
-> o100 endrepeat
-> (print, pass lit 0)
<- [MSG:INFO: PRINT, pass lit 0]

# test when using a variable
-> #<count> = 0
-> o100 repeat [#<count>]
-> (print, fail var 0)
-> o100 endrepeat
-> (print, pass var 0)
<- [MSG:INFO: PRINT, pass var 0]

# test negative repeat (should not print anything)
# todo - negative repeat should probably set an error
-> o100 repeat [-1]
-> (print, fail lit -1)
-> o100 endrepeat
-> (print, pass lit -1)
<- [MSG:INFO: PRINT, pass lit -1]

# test repeat a fixed number of times
-> #<count> = 0
-> o100 repeat [3]
-> #<count> = [#<count> + 1]
-> (print, count=%d#<count>)
<- [MSG:INFO: PRINT, count=1]
-> o100 endrepeat
<- [MSG:INFO: PRINT, count=2]
<- [MSG:INFO: PRINT, count=3]

# test repeating a variable number of times
-> #<count> = 0
-> #<i> = 3
-> o100 repeat [#<i>]
-> #<count> = [#<count> + 1]
-> (print, count=%d#<count>)
<- [MSG:INFO: PRINT, count=1]
-> o100 endrepeat
<- [MSG:INFO: PRINT, count=2]
<- [MSG:INFO: PRINT, count=3]
