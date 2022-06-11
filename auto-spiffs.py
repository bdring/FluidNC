import subprocess

subprocess.check_call (["copy", "example_configs\\4_AvaShield_7.3_K40_XYZ.yaml", "spiffs\\output\\config.yaml"], shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.check_call (["copy", "example_configs\\4_AvaShield_7.3_K40_XYZ.yaml", "FluidNC\\data\\config.yaml"], shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.check_call (["spiffs\\mkspiffs", "-c", "spiffs\\output", "spiffs\\spiffs.bin", "-s",  "196608"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.check_call (["copy", "spiffs\\spiffs.bin", "install_scripts\\win64\\wifi"], shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)