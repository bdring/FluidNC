#ifdef IDFBUILD

#    include <driver/uart.h>

#    ifdef __cplusplus
extern "C" {
#    endif

typedef void (*uart_data_callback_t)(uart_port_t uart_num, uint8_t* rx_buf, int* len);

/**
 * @brief Set data callback function
 * @param uart_num UART port number
 * @param uart_data_callback callback function
 */
void fnc_uart_set_data_callback(uart_port_t uart_num, uart_data_callback_t uart_data_callback);

esp_err_t fnc_uart_driver_install(
    uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t* uart_queue, int intr_alloc_flags);
#    ifdef __cplusplus
}
#    endif

#    define fnc_uart_set_word_length uart_set_word_length
#    define fnc_uart_get_word_length uart_get_word_length
#    define fnc_uart_set_stop_bits uart_set_stop_bits
#    define fnc_uart_get_stop_bits uart_get_stop_bits
#    define fnc_uart_set_parity uart_set_parity
#    define fnc_uart_get_parity uart_get_parity
#    define fnc_uart_set_baudrate uart_set_baudrate
#    define fnc_uart_get_baudrate uart_get_baudrate
#    define fnc_uart_set_line_inverse uart_set_line_inverse
#    define fnc_uart_set_hw_flow_ctrl uart_set_hw_flow_ctrl
#    define fnc_uart_set_sw_flow_ctrl uart_set_sw_flow_ctrl
#    define fnc_uart_get_hw_flow_ctrl uart_get_hw_flow_ctrl

#    define fnc_uart_wait_tx_done uart_wait_tx_done
#    define fnc_uart_set_pin uart_set_pin
#    define fnc_uart_flush_input uart_flush_input
#    define fnc_uart_get_buffered_data_len uart_get_buffered_data_len
#    define fnc_uart_write_bytes uart_write_bytes
#    define fnc_uart_read_bytes uart_read_bytes
#    define fnc_uart_param_config uart_param_config

#    define fnc_uart_enable_pattern_det_baud_intr uart_enable_pattern_det_baud_intr
#    define fnc_uart_pattern_pop_pos uart_pattern_pop_pos
#    define fnc_uart_pattern_get_pos uart_pattern_get_pos
#    define fnc_uart_pattern_queue_reset uart_pattern_queue_reset
#    define fnc_uart_set_mode uart_set_mode
#    define fnc_uart_set_rx_full_threshold uart_set_rx_full_threshold
#    define fnc_uart_set_tx_empty_threshold uart_set_tx_empty_threshold
#    define fnc_uart_set_rx_timeout uart_set_rx_timeout
#    define fnc_uart_get_collision_flag uart_get_collision_flag
#    define fnc_uart_set_wakeup_threshold uart_set_wakeup_threshold
#    define fnc_uart_get_wakeup_threshold uart_get_wakeup_threshold
#    define fnc_uart_wait_tx_idle_polling uart_wait_tx_idle_polling
#    define fnc_uart_set_loop_back uart_set_loop_back
#    define fnc_uart_set_always_rx_timeout uart_set_always_rx_timeout

#endif
