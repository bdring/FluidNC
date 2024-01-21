[MSG:INFO: RS485 Tx:  0x01 0x03 0x03 0x08 0x00 0x01 0x05 0x8C]
[MSG:INFO: RS485 Rx:  0x01 0x83 0x40 0x40 0xC0]

modbus frame:
- byte addr
- byte function
- n byte data
- 2 byte CRC/modbus reverse order (high byte is last byte in message)

https://crccalc.com/?crc=018340&method=CRC-16/MODBUS&datatype=hex&outtype=0


function byte:
