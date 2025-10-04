#include "NowForeverProtocol.h"

#include "Spindles/VFDSpindle.h"

namespace Spindles {
    namespace VFD {
        void NowForeverProtocol::direction_command(SpindleState mode, ModbusCommand& data) {
            data.tx_length = 9;
            data.rx_length = 6;

            data.msg[1] = 0x10;  // WRITE
            data.msg[2] = 0x09;  // Register address, high byte (spindle status)
            data.msg[3] = 0x00;  // Register address, low byte (spindle status)
            data.msg[4] = 0x00;  // Number of elements, high byte
            data.msg[5] = 0x01;  // Number of elements, low byte (1 element)
            data.msg[6] = 0x02;  // Length of first element in bytes (1 register with 2 bytes length)
            data.msg[7] = 0x00;  // Data, high byte

            /*
        Contents of register 0x0900
        Bit 0: run, 1=run, 0=stop
        Bit 1: direction, 1=ccw, 0=cw
        Bit 2: jog, 1=jog, 0=stop
        Bit 3: reset, 1=reset, 0=dont reset
        Bit 4-15: reserved
        */

            switch (mode) {
                case SpindleState::Cw:
                    data.msg[8] = 0b00000001;  // Data, low byte (run, forward)
                    log_debug("VFD: Set direction CW");
                    break;

                case SpindleState::Ccw:
                    data.msg[8] = 0b00000011;  // Data, low byte (run, reverse)
                    log_debug("VFD: Set direction CCW");
                    break;

                case SpindleState::Disable:
                    data.msg[8] = 0b00000000;  // Data, low byte (run, reverse)
                    log_debug("VFD: Disabled spindle");
                    break;

                default:
                    log_debug("VFD: Unknown spindle state");
                    break;
            }
        }

        void NowForeverProtocol::set_speed_command(uint32_t hz, ModbusCommand& data) {
            data.tx_length = 9;
            data.rx_length = 6;

            data.msg[1] = 0x10;  // WRITE
            data.msg[2] = 0x09;  // Register address, high byte (speed in hz)
            data.msg[3] = 0x01;  // Register address, low byte (speed in hz)
            data.msg[4] = 0x00;  // Number of elements, high byte
            data.msg[5] = 0x01;  // Number of elements, low byte (1 element)
            data.msg[6] = 0x02;  // Length of first element in bytes (1 register with 2 bytes length)

            /*
        Contents of register 0x0901
        Bit 0-15: speed in hz
        */

            data.msg[7] = hz >> 8;    // Data, high byte
            data.msg[8] = hz & 0xFF;  // Data, low byte

            log_debug("VFD: Set speed: " << hz / 100 << "hz or" << (hz * 60 / 100) << "rpm");
        }

        VFDProtocol::response_parser NowForeverProtocol::initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) {
            if (index == -1) {
                data.tx_length = 6;
                data.rx_length = 7;

                data.msg[1] = 0x03;  // READ
                data.msg[2] = 0x00;  // Register address, high byte (speed in hz)
                data.msg[3] = 0x07;  // Register address, low byte (speed in hz)
                data.msg[4] = 0x00;  // Number of elements, high byte
                data.msg[5] = 0x02;  // Number of elements, low byte (2 elements)

                /*
                Contents of register 0x0007
                Bit 0-15: max speed in hz * 100

                Contents of register 0x0008
                Bit 0-15: min speed in hz * 100
                */

                return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                    if (response[1] != 0x03) {
                        return false;
                    }

                    // We expect a result length of 4 bytes
                    if (response[2] != 4) {
                        return false;
                    }

                    auto nowForever = static_cast<NowForeverProtocol*>(detail);

                    nowForever->_minFrequency = (uint16_t(response[5]) << 8) | uint16_t(response[6]);
                    nowForever->_maxFrequency = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

                    log_debug("VFD: Min frequency: " << nowForever->_minFrequency
                                                     << "hz Min speed:" << (nowForever->_minFrequency * 60 / 100) << "rpm");
                    log_debug("VFD: Max frequency: " << nowForever->_maxFrequency
                                                     << "hz Max speed:" << (nowForever->_maxFrequency * 60 / 100) << "rpm");

                    nowForever->updateRPM(vfd);

                    return true;
                };
            }

            return nullptr;
        }

        void NowForeverProtocol::updateRPM(VFDSpindle* spindle) {
            if (_minFrequency > _maxFrequency) {
                uint16_t tmp  = _minFrequency;
                _minFrequency = _maxFrequency;
                _maxFrequency = tmp;
            }

            if (spindle->_speeds.size() == 0) {
                SpindleSpeed minRPM = _minFrequency * 60 / 100;
                SpindleSpeed maxRPM = _maxFrequency * 60 / 100;

                spindle->shelfSpeeds(minRPM, maxRPM);
            }
            spindle->setupSpeeds(_maxFrequency);
            spindle->_slop = std::max(_maxFrequency / 400, 1);
        }

        VFDProtocol::response_parser NowForeverProtocol::get_current_speed(ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 5;

            data.msg[1] = 0x03;  // READ
            data.msg[2] = 0x05;  // Register address, high byte (current output frequency in hz)
            data.msg[3] = 0x02;  // Register address, low byte (current output frequency in hz)
            data.msg[4] = 0x00;  // Number of elements, high byte
            data.msg[5] = 0x01;  // Number of elements, low byte (1 element)

            /*
        Contents of register 0x0502
        Bit 0-15: current output frequency in hz * 100
        */

            return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                if (response[1] != 0x03) {
                    return false;
                }

                // We expect a result length of 2 bytes
                if (response[2] != 2) {
                    return false;
                }

                // Conversion from hz to rpm not required ?
                vfd->_sync_dev_speed = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

                log_debug("VFD: Current speed: " << vfd->_sync_dev_speed / 100 << "hz or " << (vfd->_sync_dev_speed * 60 / 100) << "rpm");

                return true;
            };
        }

        VFDProtocol::response_parser NowForeverProtocol::get_current_direction(ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 5;

            data.msg[1] = 0x03;  // READ
            data.msg[2] = 0x05;  // Register address, high byte (inverter running state)
            data.msg[3] = 0x00;  // Register address, low byte (inverter running state)
            data.msg[4] = 0x00;  // Number of elements, high byte
            data.msg[5] = 0x01;  // Number of elements, low byte (1 element)

            /*
        Contents of register 0x0500
        Bit 0: run, 1=run, 0=stop
        Bit 1: direction, 1=ccw, 0=cw
        Bit 2: control, 1=local, 0=remote
        Bit 3: sight fault, 1=fault, 0=no fault
        Bit 4: fault, 1=fault, 0=no fault
        Bit 5-15: reserved
        */

            return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                bool running   = false;
                bool direction = false;  // false = cw, true = ccw

                if (response[1] != 0x03) {
                    return false;
                }

                // We expect a result length of 2 bytes
                if (response[2] != 2) {
                    return false;
                }

                running   = response[4] & 0b00000001;
                direction = (response[4] & 0b00000001) >> 1;

                //TODO: Check what to do with the inform ation we have now.
                if (running) {
                    if (direction) {
                        log_debug("VFD: Got direction CW");
                    } else {
                        log_debug("VFD: Got direction CCW");
                    }
                } else {
                    log_debug("VFD: Got spindle not running");
                }

                return true;
            };
        }

        VFDProtocol::response_parser NowForeverProtocol::get_status_ok(ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 5;

            data.msg[1] = 0x03;  // READ
            data.msg[2] = 0x03;  // Register address, high byte (current fault number)
            data.msg[3] = 0x00;  // Register address, low byte (current fault number)
            data.msg[4] = 0x00;  // Number of elements, high byte
            data.msg[5] = 0x01;  // Number of elements, low byte (1 element)

            /*
        Contents of register 0x0300
        Bit 0-15: current fault number, 0 = no fault, 1~18 = fault number
        */

            return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                uint16_t currentFaultNumber = 0;

                if (response[1] != 0x03) {
                    return false;
                }

                // We expect a result length of 2 bytes
                if (response[2] != 2) {
                    return false;
                }

                currentFaultNumber = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

                if (currentFaultNumber != 0) {
                    log_debug("VFD: Got fault number: " << currentFaultNumber);
                    return false;
                }

                return true;
            };
        }

        // Configuration registration
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, NowForeverProtocol> registration("NowForever");
        }
    }
}
