"""Utilities for compile_commands.json generation."""
import json
import os


def patch_compiler_paths(cdb_path, env):
    """Post-process compile_commands.json for clangd compatibility."""
    if not os.path.exists(cdb_path):
        return

    # Try to find toolchain - skip if not available
    try:
        platform = env.PioPlatform()
        # Try common toolchain names
        for toolchain_name in ["toolchain-gccarmnoneeabi-teensy", "toolchain-xtensa-esp32", "toolchain-atmelavr"]:
            try:
                toolchain_dir = platform.get_package_dir(toolchain_name)
                if toolchain_dir:
                    break
            except:
                continue
        else:
            return  # No toolchain found, skip patching
    except:
        return


def setup_compile_commands(env):
    """Generate compile_commands.json for clangd."""
    def _patch_action(source, target, env):
        patch_compiler_paths(str(target[0]), env)

    env.Tool("compilation_db")
    cdb_path = os.path.join(env.subst("$PROJECT_DIR"), "compile_commands.json")
    cdb = env.CompilationDatabase(cdb_path)
    env.AlwaysBuild(cdb)
    env.Default(cdb)
    env.AddPostAction(cdb, _patch_action)
