; paste with a terminal emulator with a 200 ms delay between lines
$rst=gcode
$rst=#
$#
g10 l2 p0 x0 y0 z0
g10 l2 p1 x0 y0 z0
g10 l2 p2 x0 y0 z0
g10 l2 p3 x0 y0 z0
g10 l2 p4 x0 y0 z0
g10 l2 p5 x0 y0 z0
g10 l2 p6 x0 y0 z0
?
g0 x0 y0 z0
?
G0
G0 X1
G0 I1
$G
G1
G1 Z10
$G
F1000
$G
G1 I1
G2
G2 X0 Y0 I-1 J-1
$g
G3
G3 X0 Y0 I-1 J-1
$g
g4
g4 p0
g4.1
g4.2
g5
g6
g7
g8
g9
g10
g10 l1
g10 l2
g10 l2 p0
g10 l2 p0 x1 y2 z3
g10 l2 p2 x2 y3 z4
g10 l2 p6 x3 y4 z5
$#
g10 l2 p6 x3 y4 z5 r1
g10 l2 p7 x5 y4 z3
g10 l20 p0 x5 y4 z3
$#
g10 l20 p6 x5 y4 z3
$#
g10 l20 p7 x5 y4 z3
g10 l20 p7 x5 y4 z3
g11
g12
g13
g13
g14
g15
g16

g17
$g
g18
$g
g19
$g
g17

g20
$g
g21
$g

g22
g23
g24
g25
g26
g27

g28
g28 x3
$#
g28.1
g28.1 x3
g28.2
g29
g30 y1
$#
g30.1
g30.1 y2
g30.2

g31
g32
g33
g34
g35
g36
g37
g38
g39

g40

g41
g42

g43
g43.1 x0
g43.1 z2
$#

g44
g45
g46
g47
g48

g49
$#
g49.1

g50
g51
g52
g53
g53 g0 x1
g53.1

g54
$g
g55
$g
g56
$g
g57
$g
g58
$g
g59 g0 x1
$g

g60

g61
g61.1

g62
g63
g64
g65
g66
g67
g68
g69
g70
g71
g72
g73
g74
g75
g76
g77
g78
g79

g80
$g
x1
g0 x1 
$g

g81
g82
g83
g84
g85
g86
g87
g88
g89

g90
$g
g90.1
g91
$g
g91.1

g92
g92 z1
$#
g92.1
$#
g92.1 z0
g92.2

g93
$g
g94
$g

g95
g96
g97
g98
g99

m0
?
~
?
m1
m2
?

m3
$g
m4
$g
m5
$g

m6
t2

m7
$g
m8
$g
m9
$g

m10
m55

m56

m62
m62 p0
m62 p1
m62 p4
m62 p5

m63
m63 p0
m63 p1
m63 p4
m63 p5
