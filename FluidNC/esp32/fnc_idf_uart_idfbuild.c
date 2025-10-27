/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef IDFBUILD

#    include "fnc_idf_uart.h"
#    include <driver/uart.h>
#    include <driver/uart_select.h>

// From uart.c (components/esp_driver_uart/src/uart.c):

#    include <freertos/FreeRTOS.h>
#    include <freertos/queue.h>

#    define BUF_SIZE 1024

uart_data_callback_t uart_callbacks[UART_NUM_MAX] = { 0 };
QueueHandle_t        queues[UART_NUM_MAX];

static void uart_event_task(void* pvParameters) {
    int                  port = *((int*)pvParameters);
    uart_data_callback_t cb   = uart_callbacks[port];

    uart_event_t event;
    uint8_t*     dtmp = (uint8_t*)malloc(BUF_SIZE);
    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(queues[port], (void*)&event, (TickType_t)portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                uart_read_bytes(port, dtmp, event.size, portMAX_DELAY);
                int size = event.size;
                (*cb)(port, dtmp, &size);
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

esp_err_t fnc_uart_driver_install(
    uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t* uart_queue, int intr_alloc_flags) {
    assert(uart_queue == NULL);
    return uart_driver_install(uart_num, rx_buffer_size, tx_buffer_size, BUF_SIZE * 2, &queues[uart_num], intr_alloc_flags);
}

void fnc_uart_set_data_callback(uart_port_t uart_num, uart_data_callback_t uart_data_callback) {
    static int num;  // keep alive for a bit. Yuck but it works.
    num = uart_num;
    if (uart_callbacks[(int)uart_num] == NULL) {
        //Create a task to handler UART event from ISR -- TODO FIXME: Which core?
        xTaskCreate(uart_event_task, "uart_event_task", 3072, &num, 12, NULL);
    }

    // TODO FIXME: uart_callback is not something that's always there. Some people suggest using a task and then looping - but that's really silly.
    // It's a bit foolish, but I suppose we really need to copy-paste the implementation here and extend it with the callback. :-(
    uart_callbacks[(int)uart_num] = uart_data_callback;
}

#endif
