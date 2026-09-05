Import("env")
import os


def enable_fpermissive_for_i2s_engine(env, node):
    expected_suffix = os.path.normpath("esp32/esp32/i2s_engine.cpp")
    source_path = os.path.normpath(node.get_path())
    if not source_path.endswith(expected_suffix):
        return node

    return env.Object(node, CXXFLAGS=env.get("CXXFLAGS", []) + ["-fpermissive"])


env.AddBuildMiddleware(enable_fpermissive_for_i2s_engine, "*i2s_engine.cpp")