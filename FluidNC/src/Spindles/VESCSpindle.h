#pragma once
#include "Spindle.h"
#include "../Uart.h"

namespace Spindles {

    class VESC : public Spindle {
    private:
        // https://github.com/vedderb/bldc/blob/b900ffcde534780842c581b76ceaa44c202c6054/datatypes.h#L125
        enum mc_fault_code : uint8_t {
            FAULT_CODE_NONE = 0,
        };

        // https://github.com/vedderb/bldc/blob/b900ffcde534780842c581b76ceaa44c202c6054/datatypes.h#L933
        enum comm_packet_id : uint8_t {
            COMM_SET_DUTY             = 5,
            COMM_SET_CURRENT          = 6,
            COMM_SET_RPM              = 8,
            COMM_GET_VALUES_SELECTIVE = 50,
        };

        enum vesc_control_mode : uint8_t {
            DUTY    = comm_packet_id::COMM_SET_DUTY,
            CURRENT = comm_packet_id::COMM_SET_CURRENT,
            RPM     = comm_packet_id::COMM_SET_RPM,
        };

        EnumItem _vesc_control_mode_selection[4] = { // float (0 to 1, we use 0 to 100)
                                                     { vesc_control_mode::DUTY, "Duty" },
                                                     // float (we multiply by 100. So 4.2A will be 420)
                                                     { vesc_control_mode::CURRENT, "Current" },
                                                     // int (-X to X will be -X to X)
                                                     { vesc_control_mode::RPM, "RPM" },
                                                     EnumItem(vesc_control_mode::RPM)
        };

        struct vesc_action {
            vesc_control_mode mode;
            uint32_t          value;
        };

        static QueueHandle_t _vesc_cmd_queue;
        static TaskHandle_t  _vesc_cmdTaskHandle;
        static void          vesc_cmd_task(void* pvParameters);
        static int           create_set_value_command(vesc_control_mode mode, int value, size_t buffLen, uint8_t* buffer);
        static int           create_get_fault_code_command(size_t buffLen, uint8_t* buffer);
        static bool          parse_fault_code_response(uint8_t& faultCode, size_t readByteCount, uint8_t* buffer);

        int   _control_mode_to_use;
        Uart* _uart = nullptr;
        void  set_state_internal(SpindleState state, SpindleSpeed speed);

    public:
        VESC() = default;

        VESC(const VESC&)            = delete;
        VESC(VESC&&)                 = delete;
        VESC& operator=(const VESC&) = delete;
        VESC& operator=(VESC&&)      = delete;

        void init() override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        void validate() const override {
            Spindle::validate();
            Assert(_uart != nullptr, "VESC: missing UART configuration");
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.section("uart", _uart);
            handler.item("control_type", _control_mode_to_use, _vesc_control_mode_selection);
            Spindle::group(handler);
        }

        const char* name() const override { return "VESC"; }
    };
}