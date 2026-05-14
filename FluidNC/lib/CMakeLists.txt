# HOWTO. Copy the .pio libraries in this folder. We can certainly do better in time, for now let's just do it like this as a WIP.

idf_component_register(SRCS TMCStepper/src/source/bcm2835_spi.cpp TMCStepper/src/source/bcm2835_stream.cpp 
                            TMCStepper/src/source/CHOPCONF.cpp TMCStepper/src/source/COOLCONF.cpp TMCStepper/src/source/DRVCONF.cpp TMCStepper/src/source/DRVCTRL.cpp TMCStepper/src/source/DRVSTATUS.cpp 
                            TMCStepper/src/source/DRV_CONF.cpp TMCStepper/src/source/DRV_STATUS.cpp TMCStepper/src/source/ENCMODE.cpp TMCStepper/src/source/GCONF.cpp TMCStepper/src/source/IHOLD_IRUN.cpp 
                            TMCStepper/src/source/PWMCONF.cpp TMCStepper/src/source/RAMP_STAT.cpp TMCStepper/src/source/SERIAL_SWITCH.cpp TMCStepper/src/source/SGCSCONF.cpp TMCStepper/src/source/SHORT_CONF.cpp 
                            TMCStepper/src/source/SMARTEN.cpp TMCStepper/src/source/SW_MODE.cpp TMCStepper/src/source/SW_SPI.cpp TMCStepper/src/source/TMC2130Stepper.cpp TMCStepper/src/source/TMC2160Stepper.cpp 
                            TMCStepper/src/source/TMC2208Stepper.cpp TMCStepper/src/source/TMC2209Stepper.cpp TMCStepper/src/source/TMC2660Stepper.cpp TMCStepper/src/source/TMC5130Stepper.cpp 
                            TMCStepper/src/source/TMC5160Stepper.cpp TMCStepper/src/source/TMCStepper.cpp
                            arduinoWebSockets/src/SocketIOclient.cpp arduinoWebSockets/src/WebSockets.cpp arduinoWebSockets/src/WebSocketsClient.cpp
                            arduinoWebSockets/src/WebSocketsServer.cpp arduinoWebSockets/src/libb64/cdecode.c arduinoWebSockets/src/libb64/cencode.c
                            arduinoWebSockets/src/libsha1/libsha1.c
                            SSD1306/src/OLEDDisplay.cpp SSD1306/src/OLEDDisplayUi.cpp
                       INCLUDE_DIRS "TMCStepper/src"
                       REQUIRES driver src arduino-esp32
                       PRIV_REQUIRES esp_timer)
