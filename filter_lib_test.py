Import("env")

def skip_from_build(node):
    # Example: Exclude a directory named 'extras' inside 'my-external-lib-name'
    if "Arduino-Emulator/ArduinoCore-API/test" in node.get_path():
        return None  # Return None to ignore the file from build
    if "Arduino-Emulator/ArduinoCore-Linux/cores/firmata" in node.get_path():
        return None  # Return None to ignore the file from build
    return node

# Register the callback to apply the filter to all source files
env.AddBuildMiddleware(skip_from_build, "*")
