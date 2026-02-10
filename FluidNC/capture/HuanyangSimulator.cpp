#include "HuanyangSimulator.h"

#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>

#include <iostream>

VFDSimulator::VFDSimulator(uint8_t modbus_addr) : VFDSimulator(modbus_addr, VFDParameters {}) {}

VFDSimulator::VFDSimulator(uint8_t modbus_addr, const VFDParameters& params) :
    _modbus_addr(modbus_addr), _params(params), _current_state(SpindleState::STOPPED), _target_frequency(0), _current_frequency(0),
    _output_current(0), _dc_voltage(310)  // Typical DC bus voltage for 220V AC
    ,
    _ac_voltage(220), _temperature(25), _last_update_time(0), _acceleration_time_ms(3000)  // 3 seconds acceleration
    ,
    _deceleration_time_ms(2000)  // 2 seconds deceleration
    ,
    _communication_error(false), _overload_error(false), _overvoltage_error(false) {
    log("VFD Simulator initialized - Address: " + std::to_string(_modbus_addr) +
        ", Max frequency: " + std::to_string(_params.max_frequency) + ", Min frequency: " + std::to_string(_params.min_frequency));
}

std::vector<uint8_t> VFDSimulator::processModbusMessage(const std::vector<uint8_t>& request) {
    if (request.size() < 4) {
        log("Invalid message: too short");
        return {};
    }

    // Validate message format and CRC
    if (!validateModbusMessage(request)) {
        log("Invalid message: CRC or format error");
        return {};
    }

    uint8_t addr = request[0];
    if (addr != _modbus_addr) {
        // Not for this device, ignore
        return {};
    }

    uint8_t        function    = request[1];
    const uint8_t* data        = &request[2];
    size_t         data_length = request.size() - 4;  // Exclude addr, function, and 2-byte CRC

    log("Processing command - Function: 0x" + std::to_string(function) + ", Data length: " + std::to_string(data_length));

    std::vector<uint8_t> response;

    switch (function) {
        // Huanyang does not follow the standard Modbus command set
        case 1:
            response = handleHuanyangCmd1(data, data_length);
            break;
        case 3:
            response = handleHuanyangCmd3(data, data_length);
            break;
        case 4:
            response = handleHuanyangCmd4(data, data_length);
            break;
        case 5:
            response = handleHuanyangCmd5(data, data_length);
            break;
        default:
            log("Unsupported function: 0x" + std::to_string(function));
            response = createModbusError(function, 0x01);  // Illegal function
            break;
    }

    return response;
}

std::vector<uint8_t> VFDSimulator::handleHuanyangCmd1(const uint8_t* data, size_t length) {
    if (length < 4) {
        return createModbusError(0x01, 0x03);  // Illegal data value
    }

    uint16_t start_addr = (data[0] << 8) | data[1];
    uint16_t num_items  = 1;
    uint16_t value      = 0;

    // Set frequencies and whatnot
    switch (start_addr) {
        case 0x305:  // PD005
            value = _params.max_frequency;
            break;
        case 0x30B:  // PD011
            value = _params.min_frequency;
            break;
        case 0x38F:  // PD143
            value = _params.motor_poles;
            break;
        case 0x390:  // PD144 related
            value = _params.rated_rpm;
            break;
        default:
            value = false;  // Unknown defaults to 0
            break;
    }

    uint8_t high = (value >> 8) & 0xFF;
    uint8_t low  = value & 0xFF;

    std::vector<uint8_t> response_data { data[0], data[1], high, low };

    return createModbusResponse(0x01, response_data);
}
std::vector<uint8_t> VFDSimulator::handleHuanyangCmd3(const uint8_t* data, size_t length) {
    if (length != 2 || data[0] != 1) {
        return createModbusError(0x01, 0x03);  // Illegal data value
    }

    std::vector<uint8_t> response_data { data[0], data[1] };

    // Set state based on value
    switch (data[1]) {
        case 0x01:  // Forward
            _current_state = SpindleState::FORWARD;
            break;
        case 0x11:  // Reverse
            _current_state = SpindleState::REVERSE;
            break;
        case 0x08:  // Off
            _current_state = SpindleState::STOPPED;
            break;
        default:
            break;
    }

    return createModbusResponse(0x03, response_data);
}

std::vector<uint8_t> VFDSimulator::handleHuanyangCmd4(const uint8_t* data, size_t length) {
    if (length != 4 || data[0] != 3 || data[1] != 1) {
        return createModbusError(0x01, 0x03);  // Illegal data value
    }

    uint8_t high = (_current_frequency >> 8) & 0xFF;
    uint8_t low  = _current_frequency & 0xFF;

    std::vector<uint8_t> response_data { data[0], data[1], high, low };
    return createModbusResponse(0x04, response_data);
}

std::vector<uint8_t> VFDSimulator::handleHuanyangCmd5(const uint8_t* data, size_t length) {
    if (length != 3 || data[0] != 2) {
        return createModbusError(0x01, 0x03);  // Illegal data value
    }
    _target_frequency = (data[1] << 8) | data[2];

    std::vector<uint8_t> response_data { data[0], data[1], data[2] };
    return createModbusResponse(0x05, response_data);
}

void VFDSimulator::update(uint32_t dt_ms) {
    updateSimulation(dt_ms);
    _last_update_time += dt_ms;
}

void VFDSimulator::updateSimulation(uint32_t dt_ms) {
    // Simulate acceleration/deceleration
    if (_current_frequency != _target_frequency) {
        uint16_t max_change;

        if (_current_frequency < _target_frequency) {
            // Accelerating
            max_change         = static_cast<uint16_t>((_params.max_frequency * dt_ms) / _acceleration_time_ms);
            _current_frequency = std::min(_target_frequency, static_cast<uint16_t>(_current_frequency + max_change));
        } else {
            // Decelerating
            max_change = static_cast<uint16_t>((_params.max_frequency * dt_ms) / _deceleration_time_ms);
            if (max_change >= _current_frequency) {
                _current_frequency = 0;
            } else {
                _current_frequency = std::max(_target_frequency, static_cast<uint16_t>(_current_frequency - max_change));
            }
        }
    }

    // Simulate current draw based on load
    if (isRunning()) {
        // Simple current model: base current + speed-dependent load
        uint16_t base_current = 5;  // 0.5A idle current
        uint16_t load_current = (_current_frequency * _params.max_current) / (_params.max_frequency * 2);
        _output_current       = base_current + load_current;

        // Simulate slight temperature rise when running
        if (_temperature < 45) {
            _temperature += static_cast<uint16_t>(dt_ms / 30000);  // Very slow heating
        }
    } else {
        _output_current = 0;
        // Cool down when stopped
        if (_temperature > 25) {
            _temperature -= static_cast<uint16_t>(dt_ms / 60000);  // Slower cooling
        }
    }

    // Simulate voltage variation
    _dc_voltage = 310 + (rand() % 20) - 10;  // ±10V variation
    _ac_voltage = 220 + (rand() % 10) - 5;   // ±5V variation
}

// Modbus RTU CRC calculation (compatible with FluidNC implementation)
uint16_t VFDSimulator::calculateModbusCRC(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < length; pos++) {
        crc ^= uint16_t(data[pos]); // XOR byte into least sig. byte of crc.

        for (size_t i = 8; i != 0; i--) { // Loop over each bit
            if ((crc & 0x0001) != 0) {    // If the LSB is set
                crc >>= 1;                // Shift right and XOR 0xA001
                crc ^= 0xA001;
            } else {        // Else LSB is not set
                crc >>= 1; // Just shift right
            }
        }
    }
    return crc;
}

bool VFDSimulator::validateModbusMessage(const std::vector<uint8_t>& message) {
    if (message.size() < 4) {
        return false;
    }
    
    // Calculate CRC on all bytes except the last 2
    uint16_t calculated_crc = calculateModbusCRC(message.data(), message.size() - 2);
    
    // Extract CRC from message (little-endian format)
    uint16_t message_crc = message[message.size() - 2] | (message[message.size() - 1] << 8);
    
    return calculated_crc == message_crc;
}

std::vector<uint8_t> VFDSimulator::createModbusResponse(uint8_t function, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> response;
    response.push_back(_modbus_addr);
    response.push_back(function);
    response.insert(response.end(), data.begin(), data.end());
    
    // Add CRC
    uint16_t crc = calculateModbusCRC(response.data(), response.size());
    response.push_back(crc & 0xFF);        // Low byte first
    response.push_back((crc >> 8) & 0xFF); // High byte
    
    return response;
}

std::vector<uint8_t> VFDSimulator::createModbusError(uint8_t function, uint8_t error_code) {
    std::vector<uint8_t> data = {error_code};
    return createModbusResponse(function | 0x80, data); // Set error bit
}

void VFDSimulator::log(const std::string& message) {
    if (_log_callback) {
        _log_callback("[VFD-" + std::to_string(_modbus_addr) + "] " + message);
    }
}
