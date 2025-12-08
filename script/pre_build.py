# pyright: reportUndefinedVariable=false, reportMissingImports=false
"""
PlatformIO pre-build script for Open Control Framework.

Can be used by:
- The framework itself (platformio.ini at framework root)
- Examples or consumer projects (via extra_scripts = pre:path/to/pre_build.py)

It handles:
1. compile_commands.json generation (for clangd IDE support)
   Output goes to $PROJECT_DIR (the calling project's directory)
"""
import inspect
import os
import sys

Import("env")

# Get THIS script's directory (SCons executes via exec(), so __file__ is not available)
# Use inspect to find the source file of the current frame
script_path = inspect.getframeinfo(inspect.currentframe()).filename
script_dir = os.path.dirname(os.path.abspath(script_path))
sys.path.insert(0, script_dir)

from compiledb_utils import setup_compile_commands

# Execute - compile_commands.json will be generated in $PROJECT_DIR (caller's dir)
setup_compile_commands(env)
