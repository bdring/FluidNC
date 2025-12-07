// Copyright (c) 2021 -  Marco Wagner
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "GenericProtocol.h"

#include "Spindles/VFDSpindle.h"

#include "string_util.h"
#include <algorithm>

namespace Spindles {
    namespace VFD {
        void GenericProtocol::scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM) {
            int32_t divider = 1;
            if (scale_str.empty()) {
                return;
            }
            if (scale_str[0] == '%') {
                scale_str.remove_prefix(1);
                n *= 100;
                divider *= maxRPM;
            }
            if (scale_str[0] == '*') {
                std::string_view numerator_str;
                scale_str = scale_str.substr(1);
                string_util::split_prefix(scale_str, numerator_str, '/');
                uint32_t numerator;
                if (string_util::from_decimal(numerator_str, numerator)) {
                    n *= numerator;
                } else {
                    log_error(spindle->name() << ": bad decimal number " << numerator_str);
                    return;
                }
                if (!scale_str.empty()) {
                    uint32_t denominator;
                    if (string_util::from_decimal(scale_str, denominator)) {
                        divider *= denominator;
                    } else {
                        log_error(spindle->name() << ": bad decimal number " << scale_str);
                        return;
                    }
                }
            } else if (scale_str[0] == '/') {
                std::string_view denominator_str(scale_str.substr(1));
                uint32_t         denominator;
                if (string_util::from_decimal(denominator_str, denominator)) {
                    divider *= denominator;
                } else {
                    log_error(spindle->name() << ": bad decimal number " << scale_str);
                    return;
                }
            }

            n /= divider;
        }

        bool GenericProtocol::set_data(std::string_view token, std::basic_string_view<uint8_t>& response_view, const char* name, uint32_t& data, bool is_big_endian) {
            /**
             *  Match token with name and process data accordingly
             * 
             * @param token tokens from the configured response format 
             * @param response_view data response from device
             * @param name keyword that needs to be processed
             * @param data processed data (output)
             * @return is handling of the token successful
             */
            
             // check if the that the response format starts with the specified keyword
            if (string_util::starts_with_ignore_case(token, name)) {
                // combine two-bytes from the device response into the value
                uint32_t rval;
                if(is_big_endian){
                    rval = (response_view[0] << 8) + (response_view[1] & 0xff);
                } else {
                    rval = ((response_view[1] & 0xFF) << 8) + (response_view[0] & 0xff);
                }
                // process the remaining part of the token string and adjust for scaling ([*][\][%])
                scale(rval, token.substr(strlen(name)), 1);
                data = rval;
                // remove the two-bytes that were processed
                response_view.remove_prefix(2);
                return true;
            }
            return false;
        }
        bool GenericProtocol::parser(const uint8_t* response, VFDSpindle* spindle, GenericProtocol* instance) {
            /**
             * Process the response from our request using the format specified in the configuration
             * 
             * @param response the data received back from the protocol's request
             * @param spindle information about the spindle
             * @param instance the protocol itself (RS485, etc)
             * @return is parsing of the response successful
             */
            // This routine does not know the actual length of the response array
            std::basic_string_view<uint8_t> response_view(response, VFD_RS485_MAX_MSG_SIZE);
            response_view.remove_prefix(1);  // Remove the modbus ID which has already been checked

            std::string_view token;
            std::string_view format(_response_format);  // format to parse the response against
            // 'le' is a keyword modifier
            bool _is_rx_big_endian = true;
            // for each of the tokens in the configured response format
            while (string_util::split_prefix(format, token, ' ')) {
                uint8_t val;
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }

                if (string_util::starts_with_ignore_case(token, "le")) {
                    _is_rx_big_endian = false;
                    continue;
                }

                // Sync must be in a temporary because it's volatile!
                uint32_t dev_speed;

                // handle rpm keyword
                if (set_data(token, response_view, "rpm", dev_speed, _is_rx_big_endian)) {
                    if (spindle->_debug > 1) {
                        log_info("Current speed is " << int(dev_speed));
                    }

                    // pass along the processed rpm data 
                    xQueueSend(VFD::VFDProtocol::vfd_speed_queue, &dev_speed, 0);
                    continue;
                }

                // bypass 'ignore' keywords
                uint32_t ignore;
                if (set_data(token, response_view, "ignore", ignore, _is_rx_big_endian)) {
                    continue;
                }

                // handle minrpm keyword
                if (set_data(token, response_view, "minrpm", instance->_minRPM, _is_rx_big_endian)) {
                    log_debug(spindle->name() << ": got minRPM " << instance->_minRPM);
                    continue;
                }

                // handle maxrpm keyword
                if (set_data(token, response_view, "maxrpm", instance->_maxRPM, _is_rx_big_endian)) {
                    log_debug(spindle->name() << ": got maxRPM " << instance->_maxRPM);
                    continue;
                }

                // digits in the response format must match the actual response
                if (string_util::from_hex(token, val)) {
                    if (val != response_view[0]) {
                        log_debug(spindle->name() << ": response mismatch - expected " << to_hex(val) << " got " << to_hex(response_view[0]));
                        return false;
                    }
                    response_view.remove_prefix(1);
                    continue;
                }
                log_error(spindle->name() << ": bad response token " << token);
                return false;
            }
            return true;
        }
        void GenericProtocol::send_vfd_command(const std::string cmd, ModbusCommand& data, uint32_t out) {
            /**
             * Build the data frame for the modbus command based on configured format
             * 
             * @param cmd the type of command being sent
             * @param data the modbus frame: msg to be sent and the expected size of the associated response
             * @param out data to be sent along with the command
             * 
             * 
             * - configuration line splits the send format from the response format with a '>'
             * - the send or receive format can either contain fixed values (eg hex-encoded byte) or a keyword
             * - keywords that respresent a variable: rpm, minrpm, maxrpm
             * - other keywords: echo (the response is the same as command sent), ignore, le (send/receive in little endian)
             * 
             *  for example:
             *  - a modbus frame for sending a command (write) : 06 80 05 rpm > echo
             *  - a modbus frame for requesting data (read) : 03 80 18 00 01 > 03 02 le rpm*20/4
             */

            data.tx_length = 1;
            data.rx_length = 1;
            if (cmd.empty()) {
                return;
            }

            std::string_view out_view;
            std::string_view in_view(cmd);
            string_util::split_prefix(in_view, out_view, '>');
            _response_format = in_view;  // Remember the response format for the parser

            
            // transmit frame :  set a value or request a value
            // 'rpm' and 'le' are the only keywords that is allowed in the transmit frame format
            std::string_view token;
            // 'le' is a keyword modifier
            bool _tx_is_big_endian = true;
            while (data.tx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && string_util::split_prefix(out_view, token, ' ')) {
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }
                if (string_util::starts_with_ignore_case(token, "le")) {
                    _tx_is_big_endian = false;
                } else if (string_util::starts_with_ignore_case(token, "rpm")) {
                    // adjust the data associated with the rpm based on scaling [*][/][%]
                    scale(out, token.substr(strlen("rpm")), _maxRPM);
                    // store the scaled data in the transmit frame
                    if(_tx_is_big_endian) {
                        data.msg[data.tx_length++] = out >> 8;
                        data.msg[data.tx_length++] = out & 0xff;
                    } else {
                        data.msg[data.tx_length++] = out & 0xff;
                        data.msg[data.tx_length++] = out >> 8;
                    }
                } else if (string_util::from_hex(token, data.msg[data.tx_length])) {
                    // store the transmit format token's hex value in the transmit frame
                    ++data.tx_length;
                } else {
                    log_error(spindle->name() << ":Bad hex number " << token);
                    return;
                }
            }

            // receive frame : determine the size of the expected response
            while (data.rx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && string_util::split_prefix(in_view, token, ' ')) {
                if (token == "" || string_util::starts_with_ignore_case(token, "le")) {
                    // ignore repeated spaces
                    // also, 'le' is a keyword modifier and doesn't effect the size of the response
                    continue;
                }
                uint8_t x;
                // echo means receive the same length of data that was sent
                if (string_util::equal_ignore_case(token, "echo")) {
                    data.rx_length = data.tx_length;
                    break;
                }
                else if (string_util::starts_with_ignore_case(token, "rpm") || string_util::starts_with_ignore_case(token, "minrpm") ||
                    string_util::starts_with_ignore_case(token, "maxrpm") || string_util::starts_with_ignore_case(token, "ignore")) {
                    // other keywords are received as two-bytes
                    data.rx_length += 2;
                } else if (string_util::from_hex(token, x)) {
                    // each hex value is a byte
                    ++data.rx_length;
                } else {
                    log_error(spindle->name() << ": bad hex number " << token);
                }
            }
        }
        void GenericProtocol::direction_command(SpindleState mode, ModbusCommand& data) {
            switch (mode) {
                case SpindleState::Cw:
                    send_vfd_command(_cw_cmd, data, 0);
                    break;
                case SpindleState::Ccw:
                    send_vfd_command(_ccw_cmd, data, 0);
                    break;
                default:  // SpindleState::Disable
                    send_vfd_command(_off_cmd, data, 0);
                    break;
            }
        }

        void GenericProtocol::set_speed_command(uint32_t speed, ModbusCommand& data) {
            send_vfd_command(_set_rpm_cmd, data, speed);
        }

        VFDProtocol::response_parser GenericProtocol::get_current_speed(ModbusCommand& data) {
            send_vfd_command(_get_rpm_cmd, data, 0);
            return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                auto instance = static_cast<GenericProtocol*>(protocol);
                return instance->parser(response, spindle, instance);
            };
        }

        void GenericProtocol::setup_speeds(VFDSpindle* vfd) {
            vfd->shelfSpeeds(_minRPM, _maxRPM);
            vfd->setupSpeeds(_maxRPM);
            vfd->_slop = 300;
        }
        VFDProtocol::response_parser GenericProtocol::initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) {
            // BUG:
            //
            // If we do:
            // _get_min_rpm_cmd = "03 00 0B 00 01 >  03 02 maxrpm*60";
            // _get_max_rpm_cmd = "03 00 05 00 01 >  03 02 maxrpm*60";
            //
            // then the minrpm will never be assigned, and we end up in an infinite loop.
            //
            // NOT FIXED. I'm not really sure what the best approach is. Perhaps check if after the sequence
            // something changed?

            this->spindle = vfd;
            if (_maxRPM == 0xffffffff && !_get_max_rpm_cmd.empty()) {
                send_vfd_command(_get_max_rpm_cmd, data, 0);
                return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                    auto instance = static_cast<GenericProtocol*>(protocol);
                    return instance->parser(response, spindle, instance);
                };
            }
            if (_minRPM == 0xffffffff && !_get_min_rpm_cmd.empty()) {
                send_vfd_command(_get_min_rpm_cmd, data, 0);
                return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                    auto instance = static_cast<GenericProtocol*>(protocol);
                    return instance->parser(response, spindle, instance);
                };
            }
            if (vfd->_speeds.size() == 0) {
                setup_speeds(vfd);
            }
            return nullptr;
        }

        struct VFDtype {
            const char* name;
            int8_t      disable_with_s0;
            int8_t      s0_with_disable;
            uint32_t    min_rpm;
            uint32_t    max_rpm;
            const char* cw_cmd;
            const char* ccw_cmd;
            const char* off_cmd;
            const char* set_rpm_cmd;
            const char* get_rpm_cmd;
            const char* get_min_rpm_cmd;
            const char* get_max_rpm_cmd;
        } VFDtypes[] = {
            {
                "YL620",
                -1,
                -1,
                0xffffffff,
                0xffffffff,
                "06 20 00 00 12 > echo",
                "06 20 00 00 22 > echo",
                "06 20 00 00 01 > echo",
                "06 20 01 rpm*10/60 > echo",
                "03 20 0b 00 01 > 03 02 rpm*6",
                "",
                "03 03 08 00 02 > 03 04 minrpm*60/10 maxrpm*6",
            },
            {
                "Huanyang",
                -1,
                -1,
                0xffffffff,
                0xffffffff,
                "03 01 01 > echo",
                "03 01 11 > echo",
                "03 01 08 > echo",
                "05 02 rpm*100/60 > echo",
                "04 03 01 00 00 > 04 03 01 rpm*60/100",
                "01 03 0b 00 00 > 01 03 0B minRPM*60/100",
                "01 03 05 00 00 > 01 03 05 maxRPM*60/100",
            },
            {
                "H2A",
                -1,
                -1,
                6000,
                0xffffffff,
                "06 20 00 00 01 > echo",
                "06 20 00 00 02 > echo",
                "06 20 00 00 06 > echo",
                "06 10 00 rpm%*100 > echo",
                "03 70 0C 00 01 > 03 00 02 rpm",  // or "03 70 0C 00 02 > 03 00 04 rpm 00 00",
                "",
                "03 B0 05 00 01 >  03 00 02 maxrpm",  // or "03 B0 05 00 02 >  03 00 04 maxrpm 03 F6",

            },
            {
                "H100",
                -1,
                -1,
                0xffffffff,
                0xffffffff,
                "05 00 49 ff 00 > echo",
                "05 00 4A ff 00 > echo",
                "05 00 4B ff 00 > echo",
                "06 02 01 rpm%*4 > echo",
                "04 00 00 00 02 > 04 04 rpm%*4 ignore",
                "03 00 0B 00 01 > 03 02 minrpm*60",
                "03 00 05 00 01 > 03 02 maxrpm*60",
            },
            {
                "NowForever",
                -1,
                -1,
                0xffffffff,
                0xffffffff,
                "10 09 00 00 01 02 00 01 > echo",
                "10 09 00 00 01 02 00 03 > echo",
                "10 09 00 00 01 02 00 00 > echo",
                "10 09 01 00 01 02 rpm/6 > echo",
                "03 05 02 00 01 > 03 02 rpm%*4",
                "",
                "03 00 07 00 02 >  03 04 maxrpm*6 minrpm*6",
            },
            {
                "SiemensV20",
                -1,
                -1,
                0,
                24000,
                "06 00 63 0C 7F > echo",
                "06 00 63 04 7F > echo",
                "06 00 63 0C 7E > echo",
                "06 00 64 rpm%*16384/100 > echo",
                "03 00 6E 00 01 > 03 02 rpm%*16384/100",
                "",
                "",
            },
            {
                "MollomG70",
                1,                                       // disable_with_s0
                1,                                       // s0_with_disable
                0xffffffff,                              // min_rpm
                0xffffffff,                              // max_rpm
                "06 20 00 00 01 > echo",                 // cw
                "06 20 00 00 02 > echo",                 // ccw
                "06 20 00 00 06 > echo",                 // off
                "06 10 00 rpm%*100 > echo",              // set_rpm
                "03 70 00 00 01 > 03 02 rpm*60/100",     // get_rpm
                "03 f0 0e 00 01 > 03 02 minrpm*60/100",  // get_min_rpm
                "03 f0 0c 00 01 > 03 02 maxrpm*60/100",  // get_max_rpm
            },
        };
        void GenericProtocol::afterParse() {
            _model = string_util::trim(_model);
            for (auto const& vfd : VFDtypes) {
                if (string_util::equal_ignore_case(_model, vfd.name)) {
                    log_debug("Using predefined ModbusVFD " << vfd.name);
                    if (_cw_cmd.empty()) {
                        _cw_cmd = vfd.cw_cmd;
                    }
                    if (_ccw_cmd.empty()) {
                        _ccw_cmd = vfd.ccw_cmd;
                    }
#if 0
                    if (vfd.disable_with_s0 != -1) {
                        _disable_with_s0 = vfd.disable_with_s0;
                    }
                    if (vfd.s0_with_disable != -1) {
                        s0_with_disable = vfd.s0_with_disable;
                    }
#endif
                    if (_off_cmd.empty()) {
                        _off_cmd = vfd.off_cmd;
                    }
                    if (_set_rpm_cmd.empty()) {
                        _set_rpm_cmd = vfd.set_rpm_cmd;
                    }
                    if (_get_rpm_cmd.empty()) {
                        _get_rpm_cmd = vfd.get_rpm_cmd;
                    }
                    if (_get_max_rpm_cmd.empty()) {
                        _get_max_rpm_cmd = vfd.get_max_rpm_cmd;
                    }
                    if (_get_min_rpm_cmd.empty()) {
                        _get_min_rpm_cmd = vfd.get_min_rpm_cmd;
                    }
                    return;
                }
            }
        }

        // Configuration registration
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, GenericProtocol> registration("ModbusVFD");
        }
    }
}
