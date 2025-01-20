// Copyright (c) 2021 -  Marco Wagner
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "GenericProtocol.h"

#include "../VFDSpindle.h"

#include "../../string_util.h"
#include <algorithm>

namespace Spindles {
    namespace VFD {
        bool GenericProtocol::split(std::string_view& input, std::string_view& token, const char* delims) {
            if (input.size() == 0) {
                return false;
            }
            auto pos = input.find_first_of(delims);
            if (pos != std::string_view::npos) {
                token = input.substr(0, pos);
                input = input.substr(pos + 1);
            }
            else {
                token = input;
                input = "";
            }
            return true;
        }

        bool GenericProtocol::from_decimal(std::string_view str, uint32_t& value) {
            value = 0;
            if (str.size() == 0) {
                return false;
            }
            while (str.size()) {
                if (!isdigit(str[0])) {
                    return false;
                }
                value = value * 10 + str[0] - '0';
                str = str.substr(1);
            }
            return true;
        }

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
                split(scale_str, numerator_str, "/");
                uint32_t numerator;
                if (from_decimal(numerator_str, numerator)) {
                    n *= numerator;
                }
                else {
                    log_error(spindle->name() << ": bad decimal number " << numerator_str);
                    return;
                }
                if (!scale_str.empty()) {
                    uint32_t denominator;
                    if (from_decimal(scale_str, denominator)) {
                        divider *= denominator; 
                    }
                    else {
                        log_error(spindle->name() << ": bad decimal number " << scale_str);
                        return;
                    }
                }
            }
            else if (scale_str[0] == '/') {
                std::string_view denominator_str(scale_str.substr(1));
                uint32_t         denominator;
                if (from_decimal(denominator_str, denominator)) {
                    divider *= denominator;
                }
                else {
                    log_error(spindle->name() << ": bad decimal number " << scale_str);
                    return;
                }
            }

            n /= divider;
        }

        bool GenericProtocol::from_xdigit(char c, uint8_t& value) {
            if (isdigit(c)) {
                value = c - '0';
                return true;
            }
            c = tolower(c);
            if (c >= 'a' && c <= 'f') {
                value = 10 + c - 'a';
                return true;
            }
            return false;
        }

        bool GenericProtocol::from_hex(std::string_view str, uint8_t& value) {
            value = 0;
            if (str.size() == 0 || str.size() > 2) {
                return false;
            }
            uint8_t x;
            while (str.size()) {
                value <<= 4;
                if (!from_xdigit(str[0], x)) {
                    return false;
                }
                value += x;
                str = str.substr(1);
            }
            return true;
        }
        bool GenericProtocol::set_data(std::string_view token, std::basic_string_view<uint8_t>& response_view, const char* name, uint32_t& data) {
            if (string_util::starts_with_ignore_case(token, name)) {
                uint32_t rval = (response_view[0] << 8) + (response_view[1] & 0xff);
                uint32_t orval = rval;
                scale(rval, token.substr(strlen(name)), 1);
                data = rval;
                response_view.remove_prefix(2);
                return true;
            }
            return false;
        }
        bool GenericProtocol::parser(const uint8_t* response, VFDSpindle* spindle, GenericProtocol* instance) {
            // This routine does not know the actual length of the response array
            std::basic_string_view<uint8_t> response_view(response, VFD_RS485_MAX_MSG_SIZE);
            response_view.remove_prefix(1);  // Remove the modbus ID which has already been checked

            std::string_view token;
            std::string_view format(_response_format);
            while (split(format, token, " ")) {
                uint8_t val;
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }

                // Sync must be in a temporary because it's volatile!
                uint32_t sync = spindle->_sync_dev_speed;
                if (set_data(token, response_view, "rpm", sync)) {
                    spindle->_sync_dev_speed = sync;
                    continue;
                }
                uint32_t ignore;
                if (set_data(token, response_view, "ignore", ignore)) {
                    continue; 
                } 
                if (set_data(token, response_view, "minrpm", instance->_minRPM)) {
                    log_debug(spindle->name() << ": got minRPM " << instance->_minRPM);
                    continue;
                }
                if (set_data(token, response_view, "maxrpm", instance->_maxRPM)) {
                    log_debug(spindle->name() << ": got maxRPM " << instance->_maxRPM);
                    continue;
                }
                if (from_hex(token, val)) {
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
            data.tx_length = 1;
            data.rx_length = 1;
            if (cmd.empty()) {
                return;
            }

            std::string_view out_view;
            std::string_view in_view(cmd);
            split(in_view, out_view, ">");
            _response_format = in_view;  // Remember the response format for the parser

            std::string_view token;
            while (data.tx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && split(out_view, token, " ")) {
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }
                if (string_util::starts_with_ignore_case(token, "rpm")) {
                    uint32_t oout = out;
                    scale(out, token.substr(strlen("rpm")), _maxRPM);
                    data.msg[data.tx_length++] = out >> 8;
                    data.msg[data.tx_length++] = out & 0xff;
                }
                else if (from_hex(token, data.msg[data.tx_length])) {
                    ++data.tx_length;
                }
                else {
                    log_error(spindle->name() << ":Bad hex number " << token);
                    return;
                }
            }
            while (data.rx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && split(in_view, token, " ")) {
                if (token == "") {
                    // Ignore repeated spaces
                    continue;
                }
                uint8_t x;
                if (string_util::equal_ignore_case(token, "echo")) {
                    data.rx_length = data.tx_length;
                    break;
                }
                if (string_util::starts_with_ignore_case(token, "rpm") || string_util::starts_with_ignore_case(token, "minrpm") ||
                    string_util::starts_with_ignore_case(token, "maxrpm") || string_util::starts_with_ignore_case(token, "ignore")) { 
                    data.rx_length += 2;
                }
                else if (from_hex(token, x)) {
                    ++data.rx_length;
                }
                else {
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

        // Configuration registration
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, GenericProtocol> registration("ModbusVFD");
        }
    }
}
