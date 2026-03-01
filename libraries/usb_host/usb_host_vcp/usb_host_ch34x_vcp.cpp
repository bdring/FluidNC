/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2021 WCH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb/vcp_ch34x.hpp"
#include "usb/usb_types_ch9.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_COMPILER_CXX_EXCEPTIONS
#error This component requires C++ exceptions
#endif

#define CH34X_READ_REQ  (USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE | USB_BM_REQUEST_TYPE_DIR_IN)
#define CH34X_WRITE_REQ (USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE | USB_BM_REQUEST_TYPE_DIR_OUT)

#define CH34X_CMD_READ_TYPE 0xC0
#define CH34X_CMD_READ 0x95
#define CH34X_CMD_WRITE 0x9A
#define CH34X_CMD_SERIAL_INIT 0xA1
#define CH34X_CMD_MODEM_OUT 0xA4
#define CH34X_CMD_VERSION 0x5F

// For CMD 0xA4
#define CH34X_UART_CTS 0x01
#define CH34X_UART_DSR 0x02
#define CH34X_UART_RING 0x04
#define CH34X_UART_DCD 0x08
#define CH34X_CONTROL_OUT 0x10
#define CH34X_CONTROL_DTR 0x20
#define CH34X_CONTROL_RTS 0x40

// Uart state
#define CH34X_UART_STATE 0x00
#define CH34X_UART_OVERRUN_ERROR 0x01
#define CH34X_UART_BREAK_ERROR // no define
#define CH34X_UART_PARITY_ERROR 0x02
#define CH34X_UART_FRAME_ERROR 0x06
#define CH34X_UART_RECV_ERROR 0x02
#define CH34X_UART_STATE_TRANSIENT_MASK 0x07

//CH34x Baud Rate
#define CH34x_BAUDRATE_FACTOR 1532620800
#define CH34x_BAUDRATE_DIVMAX 3

// Line Coding Register (LCR)
#define CH34x_REG_LCR          0x18
#define CH34x_LCR_ENABLE_RX    0x80
#define CH34x_LCR_ENABLE_TX    0x40
#define CH34x_LCR_MARK_SPACE   0x20
#define CH34x_LCR_PAR_EVEN     0x10
#define CH34x_LCR_ENABLE_PAR   0x08
#define CH34x_LCR_STOP_BITS_2  0x04
#define CH34x_LCR_CS8          0x03
#define CH34x_LCR_CS7          0x02
#define CH34x_LCR_CS6          0x01
#define CH34x_LCR_CS5          0x00

static const char *TAG = "CH34X";

namespace esp_usb {
CH34x::CH34x(uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx)
    : intf(interface_idx)
{
    const esp_err_t err = this->open_vendor_specific(vid, pid, this->intf, dev_config);
    if (err != ESP_OK) {
        throw (err);
    }
};

esp_err_t CH34x::line_coding_set(cdc_acm_line_coding_t *line_coding)
{
    assert(line_coding);

    // Baudrate
    if (line_coding->dwDTERate != 0) {
        uint8_t factor, divisor;
        if (calculate_baud_divisor(line_coding->dwDTERate, &factor, &divisor) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        uint16_t baud_reg_val = (factor << 8) | divisor;
        baud_reg_val |= BIT7;
        ESP_RETURN_ON_ERROR(this->send_custom_request(CH34X_WRITE_REQ, CH34X_CMD_WRITE, 0x1312, baud_reg_val, 0, NULL), TAG, "Set baudrate failed");
    }

    // Line coding
    if (line_coding->bDataBits != 0) {
        uint8_t lcr = CH34x_LCR_ENABLE_RX | CH34x_LCR_ENABLE_TX;

        switch (line_coding->bDataBits) {
        case 5:
            lcr |= CH34x_LCR_CS5;
            break;
        case 6:
            lcr |= CH34x_LCR_CS6;
            break;
        case 7:
            lcr |= CH34x_LCR_CS7;
            break;
        case 8:
            lcr |= CH34x_LCR_CS8;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }

        switch (line_coding->bParityType) {
        case 0:
            break;
        case 1:
            lcr |= CH34x_LCR_ENABLE_PAR;
            break;
        case 2:
            lcr |= CH34x_LCR_ENABLE_PAR | CH34x_LCR_PAR_EVEN;
            break;
        case 3: // Mark
        case 4:
            lcr |= CH34x_LCR_ENABLE_PAR | CH34x_LCR_MARK_SPACE;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }

        switch (line_coding->bCharFormat) {
        case 0:
            break; // 1 Stop bit
        case 2:
            lcr |= CH34x_LCR_STOP_BITS_2;
            break;
        default:
            return ESP_ERR_INVALID_ARG; // 1.5 stop bits not supported
        }

        ESP_RETURN_ON_ERROR(this->send_custom_request(CH34X_WRITE_REQ, CH34X_CMD_WRITE, 0x2518, lcr, 0, NULL), TAG,
                            "Set line coding failed");
    }

    return ESP_OK;
}

esp_err_t CH34x::set_control_line_state(bool dtr, bool rts)
{
    uint16_t wValue = 0;
    if (dtr) {
        wValue |= CH34X_CONTROL_DTR;
    }
    if (rts) {
        wValue |= CH34X_CONTROL_RTS;
    }
    return this->send_custom_request(CH34X_WRITE_REQ, CH34X_CMD_MODEM_OUT, wValue, this->intf, 0, NULL);
}

int CH34x::calculate_baud_divisor(unsigned int baud_rate, unsigned char *factor, unsigned char *divisor)
{
    unsigned char a;
    unsigned char b;
    unsigned long c;

    assert(factor);
    assert(divisor);

    switch (baud_rate) {
    case 921600:
        a = 0xf3;
        b = 7;
        break;
    case 307200:
        a = 0xd9;
        b = 7;
        break;
    default:
        if (baud_rate > 6000000 / 255) {
            b = 3;
            c = 6000000;
        } else if (baud_rate > 750000 / 255) {
            b = 2;
            c = 750000;
        } else if (baud_rate > 93750 / 255) {
            b = 1;
            c = 93750;
        } else {
            b = 0;
            c = 11719;
        }

        a = (unsigned char)(c / baud_rate);
        if (a == 0 || a == 0xFF) {
            return -1; // Can't set required baud rate
        }
        // Deal with integer division
        const int delta_0 = c / a - baud_rate;
        const int delta_1 = baud_rate - c / (a + 1);
        if (delta_0 > delta_1) {
            a++;
        }
        a = 256 - a;
        break;
    }

    *factor = a;
    *divisor = b;
    return 0;
}
}
