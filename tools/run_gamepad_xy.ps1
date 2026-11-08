$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
& "$projectDir\.venv-gamepad\Scripts\python.exe" "$projectDir\tools\gamepad_motor_controller_xy.py" @args
exit $LASTEXITCODE
