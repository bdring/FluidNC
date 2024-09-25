# GCode Fixture Tests
Basic tests sent to ESP32 hardware, to validate firmware behavior.

The `run_fixture` command is used to exercise a fixture on real hardware. Fixtures contain a list of
commands to send to the ESP32, and a list of expected responses. The test runner will send the
commands to the ESP32, and compare the responses to the expected responses.

Install the tool's dependencies with pip:
```bash
pip install -r requirements.txt
```

Supported operations:
- `# ...`: Comment
- `->`: Send a command to the ESP32
- `<-`: Expect a response from the ESP32
- `<~`: Expect an optional message from the ESP32, but on mismatch, continue the test
- `<|`: Expect one of the following responses from the ESP32

The tool can be ran with either a directory, or a single file. If a directory is provided, the tool
will run all the files ending in `.nc` in the directory.

Example, checking alarm state:
```bash
./run_fixture /dev/cu.usbserial-31320 fixtures/alarms.nc
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> $Alarm/Send=10
<- ok
<- [MSG:INFO: ALARM: Spindle Control]
Fixture fixtures/alarms.nc passed
```

Example, checking idle status reporting:
```bash
./run_fixture /dev/cu.usbserial-31320 fixtures/idle_status.nc
-> $X
<~ [MSG:INFO: Caution: Unlocked]
<- ok
-> ??
<| <Idle|MPos:0.000,0.000,0.000|FS:0,0>
Fixture fixtures/idle_status.nc passed
```
