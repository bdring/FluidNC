// Copyright (c) 2024 - FluidNC RP2040 Port
// RP2040 platform-specific headers and abstractions

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============ UART Interface ============
// Note: UART functionality is implemented in fnc_uart.cpp which uses pico-sdk directly
// void init_uart(uint uart_num, uint32_t baud_rate);
// int uart_getchar(uint uart_num);
// void uart_putchar(uint uart_num, uint8_t c);
// void uart_putstring(uint uart_num, const char* str);
// bool uart_available(uint uart_num);
// bool uart_can_write(uint uart_num);
// void uart_flush(uint uart_num);
// void uart_set_baudrate(uint uart_num, uint32_t baud_rate);

// ============ SPI Interface ============
void    init_spi(uint spi_num, uint32_t baudrate);
uint8_t spi_readwrite(uint spi_num, uint8_t value);
void    spi_write(uint spi_num, const uint8_t* buf, size_t len);
void    spi_read(uint spi_num, uint8_t* buf, size_t len);
void    spi_write_read(uint spi_num, const uint8_t* write_buf, uint8_t* read_buf, size_t len);
void    spi_set_baudrate(uint spi_num, uint32_t baudrate);
void    spi_cs_select(uint spi_num, bool select);

// ============ I2C Interface ============
void init_i2c(uint i2c_num, uint32_t baudrate);
int  i2c_write(uint i2c_num, uint8_t addr, const uint8_t* buf, size_t len);
int  i2c_read(uint i2c_num, uint8_t addr, uint8_t* buf, size_t len);
int  i2c_write_read(uint i2c_num, uint8_t addr, const uint8_t* write_buf, size_t write_len, uint8_t* read_buf, size_t read_len);
void i2c_set_baudrate(uint i2c_num, uint32_t baudrate);
bool i2c_probe(uint i2c_num, uint8_t addr);

// ============ Watchdog Interface ============
void init_watchdog(uint32_t timeout_ms);
void feed_watchdog();
void disable_watchdog();
bool watchdog_is_enabled();

// ============ Delay Interface ============
void delay_usecs(uint32_t usecs);
void delay_usecs_precision(uint32_t usecs);

// ============ Filesystem Interface ============
void init_littlefs();
void littlefs_format();
int  littlefs_mount();
int  littlefs_unmount();

// ============ Misc ============
void rp2040_init();
void rp2040_startup_message();

#ifdef __cplusplus
}
#endif
