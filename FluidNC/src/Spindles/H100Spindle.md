# Basic protocol of H100 spindle

All these things are tested

## H100 General protocol

[id] [fcn code] [start addr] [payload] [checksum]

Read F011 (min frequency) and F005 (max frequency):

[03] [000B] [0001] gives [03] [02] [xxxx] (with 02 the result byte count).
[03] [0005] [0001] gives [03] [02] [xxxx] (with 02 the result byte count).

First commands. These give the identity response if things go correctly. So:

[01] [05] [00 49] [ff 00] [0c 2c] -- forward run
[01] [05] [00 4A] [ff 00] [0c 2c] -- reverse run
[01] [05] [00 4B] [ff 00] [0c 2c] -- stop

Tracking:

[01] [04] [0000] [0002] -- output frequency
gives [01] [04] [04] [00] [00] [0F] [crc16].

01 04 |xx xx| |xx xx| |crc1 crc2|. So that's addr, cmd=04,
2x 2xdata. First data seems running freq, second data set 
freq. Running freq is *data[2,3]*.

Set frequency:
[01] [06] [0201] [07D0] Set frequency to [07D0] = 200.0 Hz. (2000 is written!)

Irrelevant but could be useful:
[01] [04] [0001] [0002] -- set frequency
