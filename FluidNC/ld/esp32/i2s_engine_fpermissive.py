Import("env")


def enable_fpermissive_for_i2s_engine(env, node):
    if not node.get_path().endswith("esp32/esp32/i2s_engine.cpp"):
        return node

    return env.Object(node, CXXFLAGS=env.get("CXXFLAGS", []) + ["-fpermissive"])


env.AddBuildMiddleware(enable_fpermissive_for_i2s_engine, "*i2s_engine.cpp")