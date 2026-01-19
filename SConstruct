#!/usr/bin/env python3
"""
Top-level SCons build script for engine-sim Godot addon.

This script orchestrates the complete build process:
  1. Configures and builds the C++ engine-core library via CMake
  2. Builds the GDExtension wrapper that links to engine-core

Usage:
    scons                                                    # Build everything with defaults
    scons platform=macos arch=arm64 target=template_debug   # Full build with specific config
    scons platform=macos arch=arm64 target=template_release # Release build
    scons -c                                                 # Clean build artifacts
    scons cmake_only=yes                                     # Only configure and build CMake portion
    scons gdext_only=yes                                     # Only build GDExtension (assumes CMake already built)
    scons -j8                                                # Build with 8 parallel jobs (passed to CMake)

Arguments:
    platform     - Target platform (default: macos)
    arch         - Target architecture (default: arm64)
    target       - Build variant: template_debug or template_release (default: template_debug)
    cmake_only   - Only build the CMake portion (yes/no, default: no)
    gdext_only   - Only build the GDExtension (yes/no, default: no)

Environment Variables:
    ENGINE_SIM_BUILD_DIR - Override CMake build directory (default: addons/engine_sim/engine-core/build)
    GODOT_CPP_PATH       - Override godot-cpp location
"""

import os
import subprocess
import sys

from SCons.Script import (
    ARGUMENTS, 
    Dir, 
    Environment,
    Default,
    GetOption,
    Help,
)

# ========================================================
# Parse arguments
# ========================================================

platform = ARGUMENTS.get("platform", "macos")
arch = ARGUMENTS.get("arch", "arm64")
build_target = ARGUMENTS.get("target", "template_debug")
cmake_only = ARGUMENTS.get("cmake_only", "no").lower() in ("1", "y", "yes", "true", "on")
gdext_only = ARGUMENTS.get("gdext_only", "no").lower() in ("1", "y", "yes", "true", "on")

# ========================================================
# Paths
# ========================================================

root_dir = Dir('#').abspath
engine_core_dir = os.path.join(root_dir, "addons", "engine_sim", "engine-core")
gdext_dir = os.path.join(root_dir, "addons", "engine_sim")
build_dir = os.environ.get("ENGINE_SIM_BUILD_DIR", os.path.join(engine_core_dir, "build"))

# ========================================================
# CMake Build Target
# ========================================================

def build_cmake(target, source, env):
    """Configure and build the engine-core C++ library via CMake."""
    
    # Configure CMake if needed
    cmake_cache = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.exists(cmake_cache):
        print("=" * 70)
        print("Configuring CMake...")
        print("=" * 70)
        os.makedirs(build_dir, exist_ok=True)
        
        cmake_config_cmd = [
            "cmake",
            "-S", engine_core_dir,
            "-B", build_dir,
            "-DPIRANHA_ENABLED=ON",
            "-DDISCORD_ENABLED=OFF",  # Disable Discord integration
        ]
        
        result = subprocess.run(cmake_config_cmd, cwd=root_dir)
        
        if result.returncode != 0:
            print("ERROR: CMake configuration failed!")
            sys.exit(1)
    
    # Build with CMake
    print("=" * 70)
    print("Building engine-core with CMake...")
    print("=" * 70)
    
    # Get number of jobs from SCons
    num_jobs = GetOption('num_jobs')
    cmake_build_cmd = ["cmake", "--build", build_dir]
    if num_jobs > 1:
        cmake_build_cmd.extend(["-j", str(num_jobs)])
    
    result = subprocess.run(cmake_build_cmd, cwd=root_dir)
    
    if result.returncode != 0:
        print("ERROR: CMake build failed!")
        sys.exit(1)
    
    print("\n✓ CMake build completed successfully\n")
    return None

# Create a phony builder for CMake
env = Environment()
cmake_target = env.Command(
    target="cmake_build_complete",
    source=None,
    action=build_cmake
)

env.AlwaysBuild(cmake_target)

# ========================================================
# GDExtension Build Target
# ========================================================

def build_gdextension(target, source, env):
    """Build the GDExtension wrapper using SCons in the addon directory."""
    
    print("=" * 70)
    print("Building GDExtension...")
    print("=" * 70)
    
    scons_args = [
        "scons",
        "-C", gdext_dir,
        f"platform={platform}",
        f"arch={arch}",
        f"target={build_target}",
    ]
    
    result = subprocess.run(scons_args, cwd=root_dir)
    
    if result.returncode != 0:
        print("ERROR: GDExtension build failed!")
        sys.exit(1)
    
    print("\n✓ GDExtension build completed successfully\n")
    return None

# Create a phony builder for GDExtension
gdext_target = env.Command(
    target="gdext_build_complete",
    source=cmake_target,  # GDExtension depends on CMake completing first
    action=build_gdextension
)

env.AlwaysBuild(gdext_target)

# ========================================================
# Default Target
# ========================================================

# Set up help message
Help("""
Engine Simulator - Godot Addon Build System
============================================

USAGE:
    scons [OPTIONS] [ARGUMENTS]

COMMON BUILD COMMANDS:
    scons                                       Build everything (CMake + GDExtension)
    scons -j8                                   Build with 8 parallel jobs
    scons platform=macos arch=arm64 target=template_debug
    scons platform=macos arch=arm64 target=template_release
    scons -c                                    Clean build artifacts

PARTIAL BUILDS:
    scons cmake_only=yes                        Only build CMake portion
    scons gdext_only=yes                        Only build GDExtension

ARGUMENTS:
    platform={macos,linux,windows}              Target platform (default: macos)
    arch={arm64,x86_64}                        Target architecture (default: arm64)
    target={template_debug,template_release}    Build variant (default: template_debug)
    cmake_only={yes,no}                         Only build CMake portion (default: no)
    gdext_only={yes,no}                         Only build GDExtension (default: no)

ENVIRONMENT VARIABLES:
    ENGINE_SIM_BUILD_DIR    Override CMake build directory
    GODOT_CPP_PATH          Override godot-cpp location

For detailed SCons options, run: scons -H
""")

if cmake_only:
    Default(cmake_target)
    print("Note: Building CMake portion only (cmake_only=yes)")
elif gdext_only:
    Default(gdext_target)
    print("Note: Building GDExtension only (gdext_only=yes)")
else:
    Default(gdext_target)  # Building GDExtension will trigger CMake first

# ========================================================
# Clean Target
# ========================================================

if GetOption('clean'):
    print("Cleaning build artifacts...")
    # SCons will handle its own clean targets
    # You can add additional clean actions here if needed
