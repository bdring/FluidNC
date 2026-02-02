#pragma once

#include <cstdint>
#include <string>
#include <functional>

#define HUANYANG 1

/**
 * VFD Simulator - Modbus RTU Protocol Implementation
 * 
 * Simulates a Variable Frequency Drive (VFD) that communicates via Modbus RTU protocol.
 * Compatible with FluidNC VFD implementations like Huanyang protocol.
 * 
 * Supported Modbus Functions:
 * - 0x03: Read Holding Registers (direction control, status)
 * - 0x04: Read Input Registers (speed, voltage, current readings)  
 * - 0x05: Write Single Coil (speed control)
 * - 0x06: Write Single Register (frequency/speed setting)
 * 
 * VFD Commands:
 * - Start Forward: 0x01 0x03 0x01 0x01 [CRC]
 * - Start Reverse: 0x01 0x03 0x01 0x11 [CRC]
 * - Stop: 0x01 0x03 0x01 0x08 [CRC]
 * - Set Speed: 0x01 0x05 0x02 [freq_high] [freq_low] [CRC]
 * - Get RPM: 0x01 0x04 0x03 0x03 0x00 0x00 [CRC]
 * - Get Status: 0x01 0x04 0x03 0x00 0x00 0x00 [CRC]
 */
// VFD Parameters (similar to Huanyang PD registers)
struct VFDParameters {
    uint16_t max_frequency  = 400;  // PD005: Maximum frequency Hz (400Hz = 24000 RPM)
    uint16_t min_frequency  = 120;  // PD011: Minimum frequency Hz (120Hz = 7200 RPM)
    uint16_t base_frequency = 400;  // PD004: Base frequency Hz
    uint16_t max_voltage    = 220;  // PD141: Max rated voltage
    uint16_t max_current    = 37;   // PD142: Max current * 10 (3.7A = 37)
    uint16_t motor_poles    = 2;    // PD143: Motor poles
    uint16_t rated_rpm      = 300;  // PD143: Rated RPM at 50 Hz
};

class VFDSimulator {
public:
    enum class SpindleState : uint8_t { STOPPED = 0x08, FORWARD = 0x01, REVERSE = 0x11, BRAKING = 0x06, ERROR = 0xFF };

    // Constructor with optional Modbus address and parameters
    VFDSimulator(uint8_t modbus_addr = 1);
    VFDSimulator(uint8_t modbus_addr, const VFDParameters& params);

    // Main interface: Process Modbus RTU message
    std::vector<uint8_t> processModbusMessage(const std::vector<uint8_t>& request);

    // VFD Status Queries
    uint16_t     getCurrentFrequency() const { return _current_frequency; }  // * 100
    uint16_t     getOutputCurrent() const { return _output_current; }        // * 10
    uint16_t     getDCVoltage() const { return _dc_voltage; }
    uint16_t     getACVoltage() const { return _ac_voltage; }
    uint16_t     getTemperature() const { return _temperature; }
    bool         isRunning() const { return _current_state != SpindleState::STOPPED; }

    // Configuration
    void                 setModbusAddress(uint8_t addr) { _modbus_addr = addr; }
    void                 setParameters(const VFDParameters& params) { _params = params; }
    const VFDParameters& getParameters() const { return _params; }

    // Simulation control
    void update(uint32_t dt_ms = 100);  // Update simulation state
    void setLogCallback(std::function<void(const std::string&)> callback) { _log_callback = callback; }

    // Make CRC function public for testing
    static uint16_t calculateModbusCRC(const uint8_t* data, size_t length);

private:
    // Modbus RTU Protocol
    bool                 validateModbusMessage(const std::vector<uint8_t>& message);
    std::vector<uint8_t> createModbusResponse(uint8_t function, const std::vector<uint8_t>& data);
    std::vector<uint8_t> createModbusError(uint8_t function, uint8_t error_code);

    // Command handlers
#if HUANYANG
    std::vector<uint8_t> handleHuanyangCmd1(const uint8_t* data, size_t length);
    std::vector<uint8_t> handleHuanyangCmd3(const uint8_t* data, size_t length);
    std::vector<uint8_t> handleHuanyangCmd4(const uint8_t* data, size_t length);
    std::vector<uint8_t> handleHuanyangCmd5(const uint8_t* data, size_t length);
#endif
    std::vector<uint8_t> handleWriteSingleRegister(const uint8_t* data, size_t length);

    // Internal helpers
    void     updateSimulation(uint32_t dt_ms);
    void     log(const std::string& message);

    // State variables
    uint8_t       _modbus_addr;
    VFDParameters _params;

    SpindleState _current_state;
    uint16_t     _target_frequency;   // * 100
    uint16_t     _current_frequency;  // * 100

    // Simulated sensor readings
    uint16_t _output_current;  // * 10 (amperes)
    uint16_t _dc_voltage;      // volts
    uint16_t _ac_voltage;      // volts
    uint16_t _temperature;     // °C

    // Simulation timing
    uint32_t _last_update_time;
    uint16_t _acceleration_time_ms;
    uint16_t _deceleration_time_ms;

    // Error states
    bool _communication_error;
    bool _overload_error;
    bool _overvoltage_error;

    std::function<void(const std::string&)> _log_callback;
};
