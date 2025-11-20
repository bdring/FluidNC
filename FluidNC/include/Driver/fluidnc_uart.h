#include "UartTypes.h"
#include "Event.h"
#include <cstdint>
#include "Driver/fluidnc_gpio.h"

class InputPin;

void uart_init(uint32_t uart_num);
void uart_mode(uint32_t uart_num, uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits);
bool uart_half_duplex(uint32_t uart_num);
int  uart_read(uint32_t uart_num, uint8_t* buf, uint32_t len, uint32_t timeout_ms);
int  uart_write(uint32_t uart_num, const uint8_t* buf, size_t len);
void uart_xon(uint32_t uart_num);
void uart_xoff(uint32_t uart_num);
void uart_sw_flow_control(uint32_t uart_num, bool on, uint32_t xon_threshold, uint32_t xoff_threshold);
bool uart_pins(uint32_t uart_num, pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin, pinnum_t cts_pin);
int  uart_buflen(uint32_t uart_num);
int  uart_bufavail(uint32_t uart_num);
void uart_discard_input(uint32_t uart_num);
bool uart_wait_output(uint32_t uart_num, uint32_t timeout_ms);

void uart_register_input_pin(uint32_t uart_num, pinnum_t pinnum, InputPin* object);
