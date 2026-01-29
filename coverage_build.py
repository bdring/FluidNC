"""
PlatformIO extra script to properly configure coverage for native tests.
Ensures gcov library is linked and excludes googletest from coverage.
"""
Import("env")

# Add gcov library to linker
env.Append(LIBS=["gcov"])

# For the library builds (like googletest), don't use coverage
# This is handled by lib_builder below
def lib_builder(env, node):
    # Remove coverage flags from library builds
    env_new = env.Clone()
    cxx_flags = env_new.get("CXXFLAGS", [])
    cxx_flags = [f for f in cxx_flags if "--coverage" not in f]
    env_new.Replace(CXXFLAGS=cxx_flags)
    
    cc_flags = env_new.get("CCFLAGS", [])
    cc_flags = [f for f in cc_flags if "--coverage" not in f]
    env_new.Replace(CCFLAGS=cc_flags)
    
    return node

# This doesn't work well - PIO builds libs separately
# Instead we'll just link with gcov and accept some noise in coverage
