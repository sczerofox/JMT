# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (from project root)
cmake -S . -B build

# Build Release
cmake --build build --config Release

# Build Debug
cmake --build build --config Debug

# The output binary is jmt.exe (no tests or linting tools configured)
```

## Project Overview

JMT is a **Windows-only** CLI tool for managing multiple JDK versions (similar to NVM for Node.js). Written in C++17 using only the Win32 API and C++ standard library — no external dependencies.

## Architecture

```
src/
├── main.cpp                      # Entry point: init, command parsing, auto-elevation
├── core/
│   ├── command_base.hpp          # Abstract base: execute(args, ctx) + getHelp()
│   ├── command_registry.hpp/cpp  # Maps command names to CommandBase instances
│   └── jmt_context.hpp          # Shared context (exeDirectory, cachePath, flags)
├── command/                      # 11 command implementations (one file pair each)
│   ├── search_command.cpp        # --force flag for cache bypass
│   ├── list_command.cpp
│   ├── use_command.cpp
│   ├── env_command.cpp           # Self-register in PATH
│   ├── remove_command.cpp        # Subcommands: env, temp, all, (default) managed paths
│   ├── download_command.cpp      # Subcommand: exe (ZIP is default)
│   ├── version_command.cpp
│   ├── shell_command.cpp         # Open new CMD with JMT interactive
│   ├── help_command.cpp
│   └── rollback_command.cpp
├── service/                      # Business logic layer
│   ├── jdk_scan_service.cpp      # Full SSD scan, cache read/write, self-healing
│   ├── java_env_service.cpp      # Read/write PATH, manage JDK bin entries
│   ├── jdk_download_service.cpp  # Download from mirrors/official, ZIP extraction
│   └── jmt_path_service.cpp      # Register/unregister JMT's own directory in PATH
├── infrastructure/               # Win32 wrappers
│   ├── registry_operator.cpp     # Read/write/delete env vars (system/user PATH, HKCU)
│   ├── elevation_helper.cpp      # IsElevated, RelaunchElevated (runas)
│   ├── path_utils.cpp            # Normalize, compare, split/manipulate PATH strings
│   └── file_lock.cpp             # Simple file-based mutex
├── network/
│   └── multi_thread_downloader.cpp  # Range-request parallel HTTP downloader
├── repl/
│   └── repl_engine.cpp           # Interactive mode loop (jmt with no args)
├── print/
│   ├── color_print.cpp           # Colored console output via WriteConsoleW
│   └── console_progress.cpp      # Progress bar display
└── utils/
    ├── utils.cpp                 # Filesystem helpers, drive detection, path join
    └── string_helper.cpp         # Split, trim, case-insensitive compare
```

## Key Design Decisions

- **Windows-only**: Uses Win32 API for registry, elevation, PATH, and drive detection. No cross-platform abstractions.
- **No external dependencies**: Everything uses Win32 API + C++ stdlib. curl support is shell-exec invoked, not linked.
- **PATH management**: JMT directly manipulates the system/user PATH environment variable in the Windows registry (via `RegSetValueEx` on `Environment` key), not `JAVA_HOME`. A managed-path list is stored in `HKCU\Software\JMT\ManagedPaths`.
- **Elevation**: Commands that modify system state (`use`, `env`, `remove`, `search`, `download`) auto-relaunch with `runas` if not already elevated. Falls back to user-level PATH on elevation failure.
- **Download**: Prefers ZIP downloads from configurable mirrors (`.repo/jdk_zip_repo.txt`). Uses `MultiThreadDownloader` with HTTP Range requests for parallel chunks.
- **Exit codes**: 0=success, 1=bad args/unknown command, 2=JDK not found, 3=permission failure, 4=network/disk error.
- **Wide strings everywhere**: All internal strings are `std::wstring` to match Win32 conventions.
- **Cache**: JDK scan results cached in `.jmt_cache` file next to jmt.exe. Self-heals on stale entries.
