@echo off
"%FLUIDNC_PYTHON%" "%~dp0integration_compiler_wrapper.py" g++ %*
