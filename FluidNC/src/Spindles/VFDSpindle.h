// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Spindle.h"
#include "../Types.h"

#include "../Uart.h"

// #define DEBUG_VFD
// #define DEBUG_VFD_ALL

namespace Spindles {
    extern Uart _uart;

    class VFD : public Spindle {
    private:
        static const int VFD_RS485_MAX_MSG_SIZE = 16;  // more than enough for a modbus message
        static const int MAX_RETRIES            = 5;   // otherwise the spindle is marked 'unresponsive'

        void set_mode(SpindleState mode, bool critical);

        int32_t  _current_dev_speed   = -1;
        uint32_t _last_speed          = 0;
        Percent  _last_override_value = 100;  // no override is 100 percent

        static QueueHandle_t vfd_cmd_queue;
        static TaskHandle_t  vfd_cmdTaskHandle;
        static void          vfd_cmd_task(void* pvParameters);

        static uint16_t ModRTU_CRC(uint8_t* buf, int msg_len);
        enum VFDactionType : uint8_t { actionSetSpeed, actionSetMode };
        struct VFDaction {
            VFDactionType action;
            bool          critical;
            uint32_t      arg;
        };

    protected:
        struct ModbusCommand {
            bool critical;  // TODO SdB: change into `uint8_t critical : 1;`: We want more flags...

            uint8_t tx_length;
            uint8_t rx_length;
            uint8_t msg[VFD_RS485_MAX_MSG_SIZE];
        };

    private:
        bool prepareSetModeCommand(SpindleState mode, ModbusCommand& data);
        bool prepareSetSpeedCommand(uint32_t speed, ModbusCommand& data);

        static void reportParsingErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length);
        static void reportCmdErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length, uint8_t id);

    protected:
        // Commands:
        virtual void direction_command(SpindleState mode, ModbusCommand& data) = 0;
        virtual void set_speed_command(uint32_t rpm, ModbusCommand& data)      = 0;

        // Commands that return the status. Returns nullptr if unavailable by this VFD (default):
        using response_parser = bool (*)(const uint8_t* response, VFD* spindle);

        virtual response_parser initialization_sequence(int index, ModbusCommand& data) { return nullptr; }
        virtual response_parser get_current_speed(ModbusCommand& data) { return nullptr; }
        virtual response_parser get_current_direction(ModbusCommand& data) { return nullptr; }
        virtual response_parser get_status_ok(ModbusCommand& data) = 0;
        virtual bool            safety_polling() const { return true; }
        bool                    use_delay_settings() const override { return true; }

        // The constructor sets these
        int     _uart_num  = -1;
        Uart*   _uart      = nullptr;
        uint8_t _modbus_id = 1;

        void setSpeed(uint32_t dev_speed);

        volatile bool _syncing;

    public:
        VFD() {}
        VFD(const VFD&) = delete;
        VFD(VFD&&)      = delete;
        VFD& operator=(const VFD&) = delete;
        VFD& operator=(VFD&&) = delete;

        void init();
        void config_message();
        void setState(SpindleState state, SpindleSpeed speed);
        void setSpeedfromISR(uint32_t dev_speed) override;

        volatile uint32_t _sync_dev_speed;
        SpindleSpeed      _slop;

        // Configuration handlers:
        void validate() override;
        void group(Configuration::HandlerBase& handler) override;

        virtual ~VFD() {}
    };
}
