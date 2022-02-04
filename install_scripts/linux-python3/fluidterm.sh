#!/bin/sh

# Install dependencies if needed
python3 -m pip install -q pyserial xmodem

# Run fluidterm
python3 fluidterm/fluidterm.py $*

