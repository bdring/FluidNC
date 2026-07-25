# Hijacks PlatformIO's "native" platform (the same one env:macos/env:linux/
# env:windows_x86 use) to build with the Emscripten toolchain instead of the
# system compiler. There is no well-maintained official PlatformIO platform
# for Emscripten, so this swaps the tool names or the native platform after
# PlatformIO has done its normal SCons setup.
#
# Requires `source ~/emsdk/emsdk_env.sh` (or equivalent) to have put
# emcc/em++/emar/emranlib on PATH before `pio run -e wasm` is invoked.

Import("env")
# Project source files (FluidNC/**, FluidNC/wasm/**) compile under projenv,
# not env -- env is used for library sources. Both need the toolchain swap.
try:
    Import("projenv")
except Exception:
    projenv = None

emsdk_tools = ["emcc", "em++", "emar", "emranlib"]
missing = [t for t in emsdk_tools if not env.WhereIs(t)]
if missing:
    print(
        "[env:wasm] Missing on PATH: {}. Run `source ~/emsdk/emsdk_env.sh` "
        "(installed at ~/emsdk) before building this environment.".format(", ".join(missing))
    )
    Exit(1)

toolchain = dict(
    CC="emcc",
    CXX="em++",
    LINK="em++",
    AR="emar",
    RANLIB="emranlib",
    PROGSUFFIX=".js",
)

# Flags that must be present at both compile and link time for a pthread
# build; kept here (rather than platformio.ini build_flags) since native
# platform doesn't reliably forward build_flags to the link step.
#
# -fexceptions: Emscripten disables C++ exception catching by default (for
# code size/speed); without it, FluidNC's own try/catch blocks (e.g.
# setup()'s around config loading) compile fine but abort the whole runtime
# the first time something actually throws, instead of being caught.
pthread_flags = ["-pthread", "-fexceptions"]
link_flags = [
    # No PROXY_TO_PTHREAD: per the direct-JS-bridge design there is no
    # blocking main()/loop() -- JS instantiates the module, then calls an
    # exported start function that spawns FreeRTOS-emulated tasks as real
    # pthreads and returns immediately, keeping the browser main thread free.
    "-sPTHREAD_POOL_SIZE=8",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sMODULARIZE=1",
    "-sEXPORT_NAME=FluidNCModule",
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap",
    "-sEXIT_RUNTIME=0",
]

for e in [env, projenv]:
    if e is None:
        continue
    e.Replace(**toolchain)
    e.Append(CCFLAGS=pthread_flags, LINKFLAGS=pthread_flags + link_flags)

# PROGSUFFIX gets reset to "" downstream by the native platform's own
# builder setup (unix binaries have no suffix), so the link step still
# writes an unsuffixed file even though it's actually the JS glue emcc
# produces alongside program.wasm. Copy it to program.js after linking
# rather than fighting that override.
import shutil


def _add_js_suffix(source, target, env):
    for t in target:
        src = str(t)
        shutil.copyfile(src, src + ".js")


# PROGSUFFIX/PROGPATH get re-substituted by BuildProgram() using whatever
# value is current at that later point in the build, which empirically
# isn't our override (still ends up "" as native unix binaries want) --
# so target the literal $BUILD_DIR/$PROGNAME path instead of relying on
# PROGSUFFIX or the "buildprog" alias resolving through it.
import os

prog_path = os.path.join(env.subst("$BUILD_DIR"), env.subst("$PROGNAME"))
env.AddPostAction(prog_path, _add_js_suffix)
