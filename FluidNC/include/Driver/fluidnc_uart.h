#include <src/UartTypes.h>
#include <src/Event.h>

class InputPin;

void uart_init(int uart_num);
void uart_mode(int uart_num, unsigned long baud, UartData dataBits, UartParity parity, UartStop stopBits);
bool uart_half_duplex(int uart_num);
int  uart_read(int uart_num, uint8_t* buf, int len, int timeout_ms);
int  uart_write(int uart_num, const uint8_t* buf, int len);
void uart_xon(int uart_num);
void uart_xoff(int uart_num);
void uart_sw_flow_control(int uart_num, bool on, int xon_threshold, int xoff_threshold);
bool uart_pins(int uart_num, int tx_pin, int rx_pin, int rts_pin, int cts_pin);
int  uart_buflen(int uart_num);
void uart_discard_input(int uart_num);
bool uart_wait_output(int uart_num, int timeout_ms);

void uart_register_input_pin(int uart_num, uint8_t pinnum, InputPin* object);
