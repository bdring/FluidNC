(GCode Conditionals Test Suite)
(Tests for LinuxCNC-style flowcontrol: if/else/endif, while, repeat, do)
(This file exercises the flowcontrol code through the gcode parser)

(=============================================================================)
(Test Infrastructure: Initialize result counters)
(=============================================================================)
#<pass> = 0 (pass count)
#<fail> = 0 (fail count)
o99 if [EXISTS[#<_verbose>] EQ 0]
  #<_verbose> = 1 (verbose output: 1=show PASSes, 0=quiet)
o99 endif

(=============================================================================)
(TEST 1: Simple IF with true condition)
(Expected: Block executes, moves to X10)
(=============================================================================)
o100 if [1]
  G0 X10
  o999 if [#<_verbose>]
    (print, TEST_1: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o100 else
  (print, TEST_1: FAILURE - condition [1] should be true)
  #<fail> = [#<fail> + 1]
o100 endif

(=============================================================================)
(TEST 2: Simple IF with false condition)
(Expected: Block skips, X remains at 0)
(=============================================================================)
o101 if [0]
  G0 X20
  (print, TEST_2: FAILURE - condition [0] should be false)
  #<fail> = [#<fail> + 1]
o101 else
  o999 if [#<_verbose>]
    (print, TEST_2: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o101 endif

(=============================================================================)
(TEST 3: IF with comparison - true)
(Expected: 100 GT 50 is true, executes)
(=============================================================================)
o102 if [100 GT 50]
  G0 Y10
  o999 if [#<_verbose>]
    (print, TEST_3: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o102 else
  (print, TEST_3: FAILURE - 100 GT 50 should be true)
  #<fail> = [#<fail> + 1]
o102 endif

(=============================================================================)
(TEST 4: IF with comparison - false)
(Expected: 50 GT 100 is false, skips)
(=============================================================================)
o103 if [50 GT 100]
  G0 Y20
  (print, TEST_4: FAILURE - 50 GT 100 should be false)
  #<fail> = [#<fail> + 1]
o103 else
  o999 if [#<_verbose>]
    (print, TEST_4: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o103 endif

(=============================================================================)
(TEST 5: IF with parameter variable)
(Expected: Set #101=42, then 42 GT 40 is true)
(=============================================================================)
#101 = 42
o104 if [#101 GT 40]
  G0 Z10
  o999 if [#<_verbose>]
    (print, TEST_5: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o104 else
  (print, TEST_5: FAILURE - 42 GT 40 should be true)
  #<fail> = [#<fail> + 1]
o104 endif

(=============================================================================)
(TEST 6: IF/ELSE - condition true)
(Expected: Takes if branch, 100 EQ 100 is true)
(=============================================================================)
o105 if [100 EQ 100]
  o999 if [#<_verbose>]
    (print, TEST_6: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o105 else
  (print, TEST_6: FAILURE - 100 EQ 100 should be true)
  #<fail> = [#<fail> + 1]
o105 endif

(=============================================================================)
(TEST 7: IF/ELSE - condition false)
(Expected: Takes else branch, 100 NE 100 is false)
(=============================================================================)
o106 if [100 NE 100]
  (print, TEST_7: FAILURE - 100 NE 100 should be false)
  #<fail> = [#<fail> + 1]
o106 else
  o999 if [#<_verbose>]
    (print, TEST_7: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o106 endif

(=============================================================================)
(TEST 8: IF/ELSEIF/ELSE - first condition true)
(Expected: Takes if branch, 100 GT 50)
(=============================================================================)
o107 if [100 GT 50]
  o999 if [#<_verbose>]
    (print, TEST_8: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o107 elseif [100 GT 25]
  (print, TEST_8: FAILURE - first condition true, should not reach elseif)
  #<fail> = [#<fail> + 1]
o107 else
  (print, TEST_8: FAILURE - first condition true, should not reach else)
  #<fail> = [#<fail> + 1]
o107 endif

(=============================================================================)
(TEST 9: IF/ELSEIF/ELSE - first false, second true)
(Expected: Takes elseif branch, 100 LT 50 false, 100 GT 50 true)
(=============================================================================)
o108 if [100 LT 50]
  (print, TEST_9: FAILURE - 100 LT 50 should be false)
  #<fail> = [#<fail> + 1]
o108 elseif [100 GT 50]
  o999 if [#<_verbose>]
    (print, TEST_9: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o108 else
  (print, TEST_9: FAILURE - second condition true, should not reach else)
  #<fail> = [#<fail> + 1]
o108 endif

(=============================================================================)
(TEST 10: IF/ELSEIF/ELSE - both false)
(Expected: Takes else branch, both conditions false)
(=============================================================================)
o109 if [100 LT 50]
  (print, TEST_10: FAILURE - 100 LT 50 should be false)
  #<fail> = [#<fail> + 1]
o109 elseif [100 LT 25]
  (print, TEST_10: FAILURE - 100 LT 25 should be false)
  #<fail> = [#<fail> + 1]
o109 else
  o999 if [#<_verbose>]
    (print, TEST_10: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o109 endif

(=============================================================================)
(TEST 11: Comparison operators - GT (greater than))
(Expected: 75 GT 50 is true)
(=============================================================================)
o110 if [75 GT 50]
  o999 if [#<_verbose>]
    (print, TEST_11: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o110 else
  (print, TEST_11: FAILURE - 75 GT 50 should be true)
  #<fail> = [#<fail> + 1]
o110 endif

(=============================================================================)
(TEST 12: Comparison operators - LT (less than))
(Expected: 25 LT 50 is true)
(=============================================================================)
o111 if [25 LT 50]
  o999 if [#<_verbose>]
    (print, TEST_12: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o111 else
  (print, TEST_12: FAILURE - 25 LT 50 should be true)
  #<fail> = [#<fail> + 1]
o111 endif

(=============================================================================)
(TEST 13: Comparison operators - LE (less than or equal))
(Expected: 50 LE 50 is true)
(=============================================================================)
o112 if [50 LE 50]
  o999 if [#<_verbose>]
    (print, TEST_13: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o112 else
  (print, TEST_13: FAILURE - 50 LE 50 should be true)
  #<fail> = [#<fail> + 1]
o112 endif

(=============================================================================)
(TEST 14: Comparison operators - GE (greater than or equal))
(Expected: 100 GE 100 is true)
(=============================================================================)
o113 if [100 GE 100]
  o999 if [#<_verbose>]
    (print, TEST_14: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o113 else
  (print, TEST_14: FAILURE - 100 GE 100 should be true)
  #<fail> = [#<fail> + 1]
o113 endif

(=============================================================================)
(TEST 15: Logical AND - both true)
(Expected: 1 AND 1 = 1 (true))
(=============================================================================)
o114 if [1 AND 1]
  o999 if [#<_verbose>]
    (print, TEST_15: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o114 else
  (print, TEST_15: FAILURE - 1 AND 1 should be true)
  #<fail> = [#<fail> + 1]
o114 endif

(=============================================================================)
(TEST 16: Logical AND - one false)
(Expected: 1 AND 0 = 0 (false))
(=============================================================================)
o115 if [1 AND 0]
  (print, TEST_16: FAILURE - 1 AND 0 should be false)
  #<fail> = [#<fail> + 1]
o115 else
  o999 if [#<_verbose>]
    (print, TEST_16: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o115 endif

(=============================================================================)
(TEST 17: Logical OR - both false)
(Expected: 0 OR 0 = 0 (false))
(=============================================================================)
o116 if [0 OR 0]
  (print, TEST_17: FAILURE - 0 OR 0 should be false)
  #<fail> = [#<fail> + 1]
o116 else
  o999 if [#<_verbose>]
    (print, TEST_17: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o116 endif

(=============================================================================)
(TEST 18: Logical OR - one true)
(Expected: 0 OR 1 = 1 (true))
(=============================================================================)
o117 if [0 OR 1]
  o999 if [#<_verbose>]
    (print, TEST_18: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o117 else
  (print, TEST_18: FAILURE - 0 OR 1 should be true)
  #<fail> = [#<fail> + 1]
o117 endif

(=============================================================================)
(TEST 19: Complex expression with AND)
(Expected: [100 GT 50 AND 200 LT 300] is true)
(=============================================================================)
o118 if [100 GT 50 AND 200 LT 300]
  o999 if [#<_verbose>]
    (print, TEST_19: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o118 else
  (print, TEST_19: FAILURE - both conditions true, AND should be true)
  #<fail> = [#<fail> + 1]
o118 endif

(=============================================================================)
(TEST 20: Complex expression with OR)
(Expected: [100 LT 50 OR 200 LT 300] is true)
(=============================================================================)
o119 if [100 LT 50 OR 200 LT 300]
  o999 if [#<_verbose>]
    (print, TEST_20: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o119 else
  (print, TEST_20: FAILURE - second condition true, OR should be true)
  #<fail> = [#<fail> + 1]
o119 endif

(=============================================================================)
(TEST 21: Arithmetic in condition - addition)
(Expected: 100 + 50 GT 100 is true)
(=============================================================================)
o120 if [100 + 50 GT 100]
  o999 if [#<_verbose>]
    (print, TEST_21: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o120 else
  (print, TEST_21: FAILURE - 100 + 50 > 100 should be true)
  #<fail> = [#<fail> + 1]
o120 endif

(=============================================================================)
(TEST 22: Arithmetic in condition - subtraction)
(Expected: 100 - 50 EQ 50 is true)
(=============================================================================)
o121 if [100 - 50 EQ 50]
  o999 if [#<_verbose>]
    (print, TEST_22: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o121 else
  (print, TEST_22: FAILURE - 100 - 50 EQ 50 should be true)
  #<fail> = [#<fail> + 1]
o121 endif

(=============================================================================)
(TEST 23: Arithmetic in condition - multiplication)
(Expected: 10 * 5 EQ 50 is true)
(=============================================================================)
o122 if [10 * 5 EQ 50]
  o999 if [#<_verbose>]
    (print, TEST_23: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o122 else
  (print, TEST_23: FAILURE - 10 * 5 EQ 50 should be true)
  #<fail> = [#<fail> + 1]
o122 endif

(=============================================================================)
(TEST 24: Arithmetic in condition - division)
(Expected: 100 / 2 EQ 50 is true)
(=============================================================================)
o123 if [100 / 2 EQ 50]
  o999 if [#<_verbose>]
    (print, TEST_24: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o123 else
  (print, TEST_24: FAILURE - 100 / 2 EQ 50 should be true)
  #<fail> = [#<fail> + 1]
o123 endif

(=============================================================================)
(TEST 25: Parameter arithmetic)
(Expected: #102 = 30; #102 + 20 GT 40 is true)
(=============================================================================)
#102 = 30
o124 if [#102 + 20 GT 40]
  o999 if [#<_verbose>]
    (print, TEST_25: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o124 else
  (print, TEST_25: FAILURE - #102 + 20 GT 40 should be true)
  #<fail> = [#<fail> + 1]
o124 endif

(=============================================================================)
(TEST 26: Multiple parameters in condition)
(Expected: #103=100, #104=50; #103 GT #104 is true)
(=============================================================================)
#103 = 100
#104 = 50
o125 if [#103 GT #104]
  o999 if [#<_verbose>]
    (print, TEST_26: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o125 else
  (print, TEST_26: FAILURE - #103 GT #104 should be true)
  #<fail> = [#<fail> + 1]
o125 endif

(=============================================================================)
(TEST 27: Nested IF - outer true, inner true)
(Expected: Both conditions true, executes inner)
(=============================================================================)
o126 if [1]
  o1261 if [1]
    o999 if [#<_verbose>]
      (print, TEST_27: PASS)
    o999 endif
    #<pass> = [#<pass> + 1]
  o1261 else
    (print, TEST_27: FAILURE - both conditions true, should execute inner)
    #<fail> = [#<fail> + 1]
  o1261 endif
o126 else
  (print, TEST_27: FAILURE - outer true, should execute)
  #<fail> = [#<fail> + 1]
o126 endif

(=============================================================================)
(TEST 28: Nested IF - outer true, inner false)
(Expected: Outer true, inner false, skips inner block)
(=============================================================================)
o127 if [1]
  o1271 if [0]
    (print, TEST_28: FAILURE - inner false, should skip)
    #<fail> = [#<fail> + 1]
  o1271 else
    o999 if [#<_verbose>]
      (print, TEST_28: PASS)
    o999 endif
    #<pass> = [#<pass> + 1]
  o1271 endif
o127 else
  (print, TEST_28: FAILURE - outer true, should execute)
  #<fail> = [#<fail> + 1]
o127 endif

(=============================================================================)
(TEST 29: Nested IF - outer false)
(Expected: Outer false, skips entire block including inner if)
(=============================================================================)
o128 if [0]
  o1281 if [1]
    (print, TEST_29: FAILURE - outer false, should skip entire block)
    #<fail> = [#<fail> + 1]
  o1281 endif
  (print, TEST_29: FAILURE - outer false, should skip)
  #<fail> = [#<fail> + 1]
o128 else
  o999 if [#<_verbose>]
    (print, TEST_29: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o128 endif

(=============================================================================)
(TEST 30: WHILE loop with counter - 3 iterations)
(Expected: Loop executes 3 times, #130 becomes 3)
(=============================================================================)
#130 = 0
o129 while [#130 LT 3]
  #130 = [#130 + 1]
o129 endwhile
o1291 if [#130 EQ 3]
  o999 if [#<_verbose>]
    (print, TEST_30: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1291 else
  (print, TEST_30: FAILURE - loop should execute 3 times, #130 = %#130)
  #<fail> = [#<fail> + 1]
o1291 endif

(=============================================================================)
(TEST 31: WHILE loop - condition false from start)
(Expected: Loop never executes, #131 stays 10)
(=============================================================================)
#131 = 10
o130 while [#131 LT 5]
  #131 = [#131 + 1]
o130 endwhile
o1301 if [#131 EQ 10]
  o999 if [#<_verbose>]
    (print, TEST_31: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1301 else
  (print, TEST_31: FAILURE - loop should not execute, #131 should stay 10)
  #<fail> = [#<fail> + 1]
o1301 endif

(=============================================================================)
(TEST 32: REPEAT loop with count)
(Expected: Loop repeats 5 times)
(=============================================================================)
#132 = 0
o131 repeat [5]
  #132 = [#132 + 1]
o131 endrepeat
o1311 if [#132 EQ 5]
  o999 if [#<_verbose>]
    (print, TEST_32: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1311 else
  (print, TEST_32: FAILURE - repeat should execute 5 times, #132 = %#132)
  #<fail> = [#<fail> + 1]
o1311 endif

(=============================================================================)
(TEST 33: REPEAT with zero count)
(Expected: Loop does not execute)
(=============================================================================)
#133 = 0
o132 repeat [0]
  #133 = [#133 + 1]
o132 endrepeat
o1321 if [#133 EQ 0]
  o999 if [#<_verbose>]
    (print, TEST_33: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1321 else
  (print, TEST_33: FAILURE - repeat 0 should not execute)
  #<fail> = [#<fail> + 1]
o1321 endif

(=============================================================================)
(TEST 34: DO-WHILE loop)
(Expected: Block executes at least once, then checks condition, #134 becomes 3)
(=============================================================================)
#134 = 0
o133 do
  #134 = [#134 + 1]
o133 while [#134 LT 3]
o1331 if [#134 EQ 3]
  o999 if [#<_verbose>]
    (print, TEST_34: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1331 else
  (print, TEST_34: FAILURE - do-while should execute, #134 = %#134)
  #<fail> = [#<fail> + 1]
o1331 endif

(=============================================================================)
(TEST 35: DO-WHILE with false initial condition)
(Expected: Block executes once, then stops, #135 becomes 11)
(=============================================================================)
#135 = 10
o134 do
  #135 = [#135 + 1]
o134 while [#135 LT 5]
o1341 if [#135 EQ 11]
  o999 if [#<_verbose>]
    (print, TEST_35: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1341 else
  (print, TEST_35: FAILURE - do-while should execute once, #135 = %#135)
  #<fail> = [#<fail> + 1]
o1341 endif

(=============================================================================)
(TEST 36: Conditional with BREAK in loop)
(Expected: Loop breaks when #136 EQ 5, #136 becomes 5)
(=============================================================================)
#136 = 0
o135 while [#136 LT 10]
  o1351 if [#136 EQ 5]
    o135 break
  o1351 endif
  #136 = [#136 + 1]
o135 endwhile
o1352 if [#136 EQ 5]
  o999 if [#<_verbose>]
    (print, TEST_36: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1352 else
  (print, TEST_36: FAILURE - break should exit loop at #136=5)
  #<fail> = [#<fail> + 1]
o1352 endif

(=============================================================================)
(TEST 37: Conditional with CONTINUE in loop)
(Expected: Skip to next iteration when #137 EQ 3, loop completes 5 times)
(=============================================================================)
#137 = 0
#1371 = 0
o136 while [#137 LT 5]
  #137 = [#137 + 1]
  o1361 if [#137 EQ 3]
    o136 continue
  o1361 endif
  #1371 = [#1371 + 1]
o136 endwhile
o1362 if [#137 EQ 5 AND #1371 EQ 4]
  o999 if [#<_verbose>]
    (print, TEST_37: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1362 else
  (print, TEST_37: FAILURE - loop should execute 5 times, process 4)
  #<fail> = [#<fail> + 1]
o1362 endif

(=============================================================================)
(TEST 38: Conditional with parameter-based validation)
(Expected: Use parameter in condition)
(=============================================================================)
#207 = 100
#2071 = 0
o142 if [#207 GT 0]
  #2071 = 1
  (Condition true)
o142 else
  #2071 = 0
  (Condition false)
o142 endif
o1421 if [#2071 EQ 1]
  o999 if [#<_verbose>]
    (print, TEST_38: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1421 else
  (print, TEST_38: FAILURE - parameter condition should be true)
  #<fail> = [#<fail> + 1]
o1421 endif

(=============================================================================)
(TEST 39: Multiple nested conditions)
(Expected: Complex nested logic, all conditions true)
(=============================================================================)
#300 = 50
#301 = 100
#3001 = 0
o143 if [#300 GT 25]
  o1431 if [#301 LT 150]
    o14311 if [#300 + #301 GT 100]
      #3001 = 1
    o14311 endif
  o1431 endif
o143 endif
o1432 if [#3001 EQ 1]
  o999 if [#<_verbose>]
    (print, TEST_39: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1432 else
  (print, TEST_39: FAILURE - all nested conditions should be true)
  #<fail> = [#<fail> + 1]
o1432 endif

(=============================================================================)
(TEST 40: Conditions with array-like parameter access)
(Expected: Use computed parameter addresses)
(=============================================================================)
#400 = 1
#401 = 100
#402 = 50
#4001 = 0
o144 if [#[400 + 1] GT #[400 + 2]]
  #4001 = 1
o144 else
  #4001 = 0
o144 endif
o1441 if [#4001 EQ 1]
  o999 if [#<_verbose>]
    (print, TEST_40: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1441 else
  (print, TEST_40: FAILURE - #401 GT #402 should be true)
  #<fail> = [#<fail> + 1]
o1441 endif

(=============================================================================)
(TEST 41: Comment with parentheses before IF)
(Expected: Comment should be stripped before parsing condition)
(=============================================================================)
#500 = 0
(This is a comment) o145 if [100 GT 50]
  o999 if [#<_verbose>]
    (print, TEST_41: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #500 = 1
o145 else (comment after else)
  (print, TEST_41: FAILURE - comment should not affect parsing)
  #<fail> = [#<fail> + 1]
  #500 = 0
o145 endif (comment after endif)

(=============================================================================)
(TEST 42: Comment with semicolon format before IF)
(Expected: Semicolon comments should be stripped)
(=============================================================================)
#501 = 0
; This is a semicolon comment
o146 if [100 GT 50]
  o999 if [#<_verbose>]
    (print, TEST_42: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #501 = 1
o146 else ; comment after else
  (print, TEST_42: FAILURE - comment should not affect parsing)
  #<fail> = [#<fail> + 1]
  #501 = 0
o146 endif ; comment after endif

(=============================================================================)
(TEST 43: ENDIF with inline parenthesis comment)
(Expected: Parsing ENDIF with trailing comment should work)
(This was a bug: ENDIF lines with comments broke parsing)
(=============================================================================)
#502 = 0
o147 if [1]
  o999 if [#<_verbose>]
    (print, TEST_43: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #502 = 1
o147 else
  (print, TEST_43: FAILURE - endif comment should not break parsing)
  #<fail> = [#<fail> + 1]
  #502 = 0
o147 endif (this comment should not break parsing)

(=============================================================================)
(TEST 44: ENDIF with semicolon comment)
(Expected: Parsing ENDIF with semicolon comment should work)
(=============================================================================)
#503 = 0
o148 if [1]
  o999 if [#<_verbose>]
    (print, TEST_44: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #503 = 1
o148 else
  (print, TEST_44: FAILURE - endif semicolon should not break parsing)
  #<fail> = [#<fail> + 1]
  #503 = 0
o148 endif ; semicolon comment should not break parsing

(=============================================================================)
(TEST 45: ELSEIF with inline parenthesis comment)
(Expected: ELSEIF with comment should parse correctly)
(=============================================================================)
#504 = 0
o149 if [0]
  (print, TEST_45: FAILURE - should take elseif)
  #<fail> = [#<fail> + 1]
  #504 = 0
o149 elseif [1] (comment on elseif line)
  o999 if [#<_verbose>]
    (print, TEST_45: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #504 = 1
o149 endif

(=============================================================================)
(TEST 46: ELSEIF with semicolon comment)
(Expected: ELSEIF with semicolon comment should parse correctly)
(=============================================================================)
#505 = 0
o150 if [0]
  (print, TEST_46: FAILURE - should take elseif)
  #<fail> = [#<fail> + 1]
  #505 = 0
o150 elseif [1] ; semicolon comment on elseif
  o999 if [#<_verbose>]
    (print, TEST_46: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #505 = 1
o150 endif

(=============================================================================)
(TEST 47: ELSE with inline parenthesis comment)
(Expected: ELSE with comment should parse correctly)
(=============================================================================)
#506 = 0
o151 if [0]
  (print, TEST_47: FAILURE - should take else)
  #<fail> = [#<fail> + 1]
  #506 = 0
o151 else (comment on else line)
  o999 if [#<_verbose>]
    (print, TEST_47: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #506 = 1
o151 endif

(=============================================================================)
(TEST 48: ELSE with semicolon comment)
(Expected: ELSE with semicolon comment should parse correctly)
(=============================================================================)
#507 = 0
o152 if [0]
  (print, TEST_48: FAILURE - should take else)
  #<fail> = [#<fail> + 1]
  #507 = 0
o152 else ; semicolon comment on else
  o999 if [#<_verbose>]
    (print, TEST_48: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #507 = 1
o152 endif

(=============================================================================)
(TEST 49: Multiple nested ENDIFs with comments)
(Expected: Each ENDIF with comment should parse correctly)
(=============================================================================)
#508 = 0
o153 if [1]
  o1531 if [1]
    o999 if [#<_verbose>]
      (print, TEST_49: PASS)
    o999 endif
    #<pass> = [#<pass> + 1]
    #508 = 1
  o1531 endif (nested endif with comment)
o153 endif (outer endif with comment)

(=============================================================================)
(TEST 50: WHILE with inline comment)
(Expected: WHILE with comment should parse correctly)
(=============================================================================)
#509 = 0
#5000 = 0
o154 while [#509 LT 3] (while with comment)
  #509 = [#509 + 1]
  #5000 = [#5000 + 1]
o154 endwhile (endwhile with comment)
o1541 if [#5000 EQ 3]
  o999 if [#<_verbose>]
    (print, TEST_50: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1541 else
  (print, TEST_50: FAILURE - while with comment should work)
  #<fail> = [#<fail> + 1]
o1541 endif

(=============================================================================)
(TEST 51: REPEAT with inline comment)
(Expected: REPEAT with comment should parse correctly)
(=============================================================================)
#510 = 0
o155 repeat [5] (repeat with inline comment)
  #510 = [#510 + 1]
o155 endrepeat (endrepeat with comment)
o1551 if [#510 EQ 5]
  o999 if [#<_verbose>]
    (print, TEST_52: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1551 else
  (print, TEST_52: FAILURE - repeat with comment should work)
  #<fail> = [#<fail> + 1]
o1551 endif

(=============================================================================)
(TEST 53: DO-WHILE with comments on both ends)
(Expected: Both DO and WHILE with comments should work)
(=============================================================================)
#511 = 0
o156 do (do with comment)
  #511 = [#511 + 1]
o156 while [#511 LT 2] (while with comment after do)
o1561 if [#511 EQ 2]
  o999 if [#<_verbose>]
    (print, TEST_53: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1561 else
  (print, TEST_53: FAILURE - do-while with comment should work)
  #<fail> = [#<fail> + 1]
o1561 endif

(=============================================================================)
(TEST 54: IF with complex expression and comment)
(Expected: Complex condition with comment should parse correctly)
(=============================================================================)
#512 = 0
o157 if [100 GT 50 AND 200 LT 300] (complex condition with comment)
  o999 if [#<_verbose>]
    (print, TEST_54: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #512 = 1
o157 else (else with comment)
  (print, TEST_54: FAILURE - complex condition should be true)
  #<fail> = [#<fail> + 1]
o157 endif (endif with comment)

(=============================================================================)
(TEST 55: IF with parameter and comment)
(Expected: Parameter reference with comment should work)
(=============================================================================)
#600 = 75
#601 = 0
o158 if [#600 GT 50] (checking parameter with comment)
  o999 if [#<_verbose>]
    (print, TEST_55: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #601 = 1
o158 else (else with comment)
  (print, TEST_55: FAILURE - parameter check should work)
  #<fail> = [#<fail> + 1]
o158 endif (parameter test complete)

(=============================================================================)
(TEST 56: Nested conditionals with comments on all lines)
(Expected: Comments on every line should not break parsing)
(=============================================================================)
#601 = 0
o159 if [1] (outer if with comment)
  o1591 if [1] (inner if with comment)
    o999 if [#<_verbose>]
      (print, TEST_56: PASS)
    o999 endif
    #<pass> = [#<pass> + 1]
    #601 = 1
  o1591 else (inner else with comment)
    (print, TEST_56: FAILURE)
    #<fail> = [#<fail> + 1]
  o1591 endif (inner endif with comment)
o159 else (outer else with comment)
  (print, TEST_56: FAILURE)
  #<fail> = [#<fail> + 1]
o159 endif (outer endif with comment)

(=============================================================================)
(TEST 57: IF with comment between label and keyword)
(Expected: Comment position should not affect parsing)
(=============================================================================)
#602 = 0
o160 (comment between label and if) if [1]
  o999 if [#<_verbose>]
    (print, TEST_57: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #602 = 1
o160 else
  (print, TEST_57: FAILURE)
  #<fail> = [#<fail> + 1]
o160 endif

(=============================================================================)
(TEST 58: Multiple comments on ENDIF line)
(Expected: Multiple comment formats on same line should work)
(=============================================================================)
#603 = 0
o161 if [1]
  o999 if [#<_verbose>]
    (print, TEST_58: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
  #603 = 1
o161 else
  (print, TEST_58: FAILURE)
  #<fail> = [#<fail> + 1]
o161 endif (comment after label) ; and semicolon comment

(=============================================================================)
(TEST 59: BREAK with comment)
(Expected: BREAK statement with comment should work)
(=============================================================================)
#604 = 0
o162 while [#604 LT 10] (while loop with comment)
  o1621 if [#604 EQ 5] (break condition with comment)
    o162 break (break with comment)
  o1621 else
    (inner else)
  o1621 endif (inner endif with comment)
  #604 = [#604 + 1]
o162 endwhile (endwhile with comment)
o1622 if [#604 EQ 5]
  o999 if [#<_verbose>]
    (print, TEST_59: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1622 else
  (print, TEST_59: FAILURE - break with comment should work)
  #<fail> = [#<fail> + 1]
o1622 endif

(=============================================================================)
(TEST 60: CONTINUE with comment)
(Expected: CONTINUE statement with comment should work)
(=============================================================================)
#605 = 0
#606 = 0
o163 while [#605 LT 5] (while with comment)
  #605 = [#605 + 1]
  o1631 if [#605 EQ 3] (skip condition with comment)
    o163 continue (continue with comment)
  o1631 else
    #606 = [#606 + 1]
  o1631 endif (inner endif with comment)
o163 endwhile (endwhile with comment)
o1632 if [#605 EQ 5 AND #606 EQ 4]
  o999 if [#<_verbose>]
    (print, TEST_60: PASS)
  o999 endif
  #<pass> = [#<pass> + 1]
o1632 else
  (print, TEST_60: FAILURE - continue with comment should work)
  #<fail> = [#<fail> + 1]
o1632 endif

(=============================================================================)
(End of test suite)
(All tests completed including comment handling)
(=============================================================================)

(=============================================================================)
(TEST RESULTS SUMMARY)
(=============================================================================)
(print, TEST RESULTS: Passed: %d#<pass>  Failed: %d#<fail>)
o200 if [#<pass> EQ 59 AND #<fail> EQ 0]
  (print, ALL 59 TESTS PASSED)
o200 else
  (print, SOME TESTS FAILED or INCOMPLETE)
o200 endif

M2
