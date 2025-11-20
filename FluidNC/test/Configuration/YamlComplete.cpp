#include "TestFramework.h"

#include <src/Configuration/Tokenizer.h>
#include <src/Configuration/Parser.h>

namespace Configuration {
    Test(YamlComplete, Test) {
        const char* config = "name: \"ESP32 Dev Controller V4\"\n"
                             "board: \"ESP32 Dev Controller V4\"\n"
                             "yaml_wiki: \"https://github.com/bdring/FluidNC/wiki/YAML-Config-File\"\n"
                             "\n"
                             "idle_time: 250\n"
                             "engine: rmt\n"
                             "dir_delay_microseconds: 1\n"
                             "pulse_microseconds: 2\n"
                             "disable_delay_us: 0\n"
                             "homing_init_lock: false\n"
                             "\n"
                             "axes:\n"
                             "  number_axis: 3\n"
                             "  shared_stepper_disable_pin: gpio.13:low\n"
                             "  \n"
                             "  x:\n"
                             "\n"
                             "  y:\n"
                             "\n"
                             "  z:\n"
                             "\n"
                             "\n"
                             "coolant:\n"
                             "  flood: gpio.25:low\n"
                             "  mist:  gpio.21\n"
                             "\n"
                             "comms:\n"
                             "    wifi_sta:\n"
                             "        ssid: StefanMieke\n"
                             "\n"
                             "    wifi_ap:\n"
                             "        ip_address: \"192.168.0.1\"\n"
                             "        ssid: ScratchThat\n"
                             "        \n"
                             "probe:\n"
                             "    pin: gpio.32:high:pu\n"
                             "\n";

        Parser p(config, config + strlen(config));
        p.Tokenize();
        {
            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("name"), "Expected 'name'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("board"), "Expected 'board'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("yaml_wiki"), "Expected 'yaml_wiki'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("idle_time"), "Expected 'idle_time'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("engine"), "Expected 'engine'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("dir_delay_microseconds"), "Expected 'dir_delay_microseconds'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("pulse_microseconds"), "Expected 'pulse_microseconds'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("disable_delay_us"), "Expected 'disable_delay_us'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("homing_init_lock"), "Expected 'homing_init_lock'");
            p.Tokenize();

            Assert(!p.Eof(), "No EOF expected");
            Assert(p.key().equals("axes"), "Expected 'axes'");
            {
                p.Tokenize();

                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("number_axis"), "Expected 'number_axis'");
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("shared_stepper_disable"), "Expected 'shared_stepper_disable'");
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("x"), "Expected 'x'");
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("y"), "Expected 'y'");
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("z"), "Expected 'z'");
            }
            Assert(!p.Eof(), "No EOF expected");
            p.Tokenize();

            Assert(p.key().equals("coolant"), "Expected 'coolant'");
            {
                p.Tokenize();

                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("flood"), "Expected 'flood'");
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("mist"), "Expected 'mist'");
            }
            Assert(!p.Eof(), "No EOF expected");
            p.Tokenize();

            Assert(p.key().equals("comms"), "Expected 'comms'");
            {
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("wifi_sta"), "Expected 'wifi_sta'");
                {
                    p.Tokenize();
                    Assert(!p.Eof(), "No EOF expected");
                    Assert(p.key().equals("ssid"), "Expected 'ssid'");
                }
                Assert(!p.Eof(), "No EOF expected");
                p.Tokenize();
                Assert(p.key().equals("wifi_ap"), "Expected 'wifi_ap'");
                {
                    p.Tokenize();
                    Assert(!p.Eof(), "No EOF expected");
                    Assert(p.key().equals("ip_address"), "Expected 'ip_address'");
                    p.Tokenize();
                    Assert(!p.Eof(), "No EOF expected");
                    Assert(p.key().equals("ssid"), "Expected 'ssid'");
                }
            }
            Assert(!p.Eof(), "No EOF expected");
            p.Tokenize();

            Assert(p.key().equals("probe"), "Expected 'probe'");
            {
                p.Tokenize();
                Assert(!p.Eof(), "No EOF expected");
                Assert(p.key().equals("pin"), "Expected 'pin'");
            }
            p.Tokenize();
            Assert(p.Eof(), "No EOF expected");
        }
    }
}
