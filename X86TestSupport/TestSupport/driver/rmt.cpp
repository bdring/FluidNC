#include "rmt.h"

rmt_dev_t RMT;

esp_err_t rmt_set_source_clk(rmt_channel_t channel, rmt_source_clk_t base_clk) {
    return ESP_OK;
}

esp_err_t rmt_config(const rmt_config_t* rmt_param) {
    // TODO: figure out the stuff that matter, and push them.
    return ESP_OK;
}
esp_err_t rmt_fill_tx_items(rmt_channel_t channel, const rmt_item32_t* item, uint16_t item_num, uint16_t mem_offset) {
    // TODO: figure out the stuff that matter, and push them.
    return ESP_OK;
}
