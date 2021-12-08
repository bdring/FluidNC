## Compiling Fluidterm to an Executable

You have to do this on the host system on which you wish to run
the executable.

  python3 -m pip install pyinstaller
  python3 -m PyInstaller --oneline fluidterm.py

The output is in dist\fluidterm.exe (Windows) or dist\fluidterm
(Linux and MacOS).  On Linux and Mac, the executable may depend
on versions of libraries that might not present on your system.
On those platforms, it can sometimes be easier to run from the
source code, with:

  python3 -m pip install -q pyserial xmodem  # Only need to do this once
  python3 fluidterm.py

  
