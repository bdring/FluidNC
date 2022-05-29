import subprocess

subprocess.run (["copy", "example_configs\\4_AvaShield_7.3_K40.yaml", "spiffs\\output\\config.yaml"], shell=True)
subprocess.run (["spiffs\\mkspiffs", "-c", "spiffs\\output", "spiffs\\spiffs.bin", "-s",  "196608"])
subprocess.run (["copy", "spiffs\\spiffs.bin", "install_scripts\\win64\\wifi"], shell=True)
