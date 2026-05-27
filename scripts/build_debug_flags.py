import os

Import("env")


def env_flag_enabled(name):
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


debug_build = env_flag_enabled("OPENELM_DEBUG_BUILD")
level = os.environ.get("OPENELM_CORE_DEBUG_LEVEL", "0").strip()

if not level.isdigit() or int(level) < 0 or int(level) > 5:
    raise ValueError("OPENELM_CORE_DEBUG_LEVEL must be 0..5")

env.Append(CPPDEFINES=[("CORE_DEBUG_LEVEL", int(level))])
env.Append(CCFLAGS=["-g3"])
env.Append(CXXFLAGS=["-g3"])
env.Append(LINKFLAGS=["-g3"])

if debug_build:
    env.Append(CCFLAGS=["-Og"])
    env.Append(CXXFLAGS=["-Og"])

print("OpenELM32: CORE_DEBUG_LEVEL=%s%s" %
      (level, " low-optimization debug build=on" if debug_build else " debug symbols=on"))
