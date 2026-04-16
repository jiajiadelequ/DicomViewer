# Diagnostics Integration Log

Last updated: 2026-04-16

This document records the crash-diagnostics capabilities currently integrated into this project, so future debugging work can quickly determine what tooling is already available.

## Current diagnostics coverage

The project currently includes these four layers:

1. `WER LocalDumps`
2. `Visual Studio / WinDbg + PDB`
3. `Qt file logging`
4. `AddressSanitizer`

## 1. WER LocalDumps

Purpose:

- collect OS-managed crash dumps without requiring application code changes at crash time
- preserve a post-mortem dump even if the in-process crash handler is bypassed

Repository integration:

- enable script: [tools/enable-wer-localdumps.ps1](F:\QtProject\DicomViewer\tools\enable-wer-localdumps.ps1:1)
- disable script: [tools/disable-wer-localdumps.ps1](F:\QtProject\DicomViewer\tools\disable-wer-localdumps.ps1:1)

Operational notes:

- requires an elevated PowerShell session
- default dump folder is `%LOCALAPPDATA%\DicomViewerSkeleton\wer-dumps`
- intended for real crash reproduction outside the debugger as well

What it helps locate:

- hard crashes
- access violations
- stack corruption symptoms
- faults that occur before normal application-level recovery logic can run

## 2. Visual Studio / WinDbg + PDB

Purpose:

- analyze `.dmp` files with symbols
- recover call stacks, modules, threads, and exception context

Repository integration:

- `RelWithDebInfo` build script: [build-relwithdebinfo-msvc.cmd](F:\QtProject\DicomViewer\.vscode\build-relwithdebinfo-msvc.cmd:1)
- VS Code task: [tasks.json](F:\QtProject\DicomViewer\.vscode\tasks.json:1)
- VS Code launch profile: [launch.json](F:\QtProject\DicomViewer\.vscode\launch.json:1)
- build configuration support in [CMakeLists.txt](F:\QtProject\DicomViewer\CMakeLists.txt:1)

Operational notes:

- `RelWithDebInfo` is the preferred reproduction mode for release-like failures
- keep the matching `.pdb` files from the same build alongside the dump analysis workflow
- third-party library path selection was adjusted so `RelWithDebInfo` reuses the bundled `Release` runtime layout

What it helps locate:

- exact crashing function
- faulting thread
- module ownership of the crash
- optimized-build-only failures that do not reproduce in plain Debug

## 3. Qt file logging

Purpose:

- record the last successful operations before a failure
- capture thread-aware runtime logs from Qt and project categories

Repository integration:

- logging initialization in [main.cpp](F:\QtProject\DicomViewer\main.cpp:1)
- in-process diagnostics handler in [crashhandler.cpp](F:\QtProject\DicomViewer\src\core\runtime\crashhandler.cpp:1)
- message pattern and logging switches in [CMakeLists.txt](F:\QtProject\DicomViewer\CMakeLists.txt:1)

Current behavior:

- log lines include time, level, thread id, category, source file, source line, function, and message
- existing `qInfo/qWarning/qCritical` and `QLoggingCategory` output is routed into the file log
- current study-load flow has explicit logging around open, cancel, success, and failure paths

Default output locations:

- Qt/session logs: `%LOCALAPPDATA%\DicomViewer\DicomViewerSkeleton\logs`
- in-process crash dumps: `%LOCALAPPDATA%\DicomViewer\DicomViewerSkeleton\crashdumps`

What it helps locate:

- which load stage failed
- what the user opened immediately before failure
- whether the crash followed a warning/error sequence
- which thread emitted the last recorded diagnostics

## 4. AddressSanitizer

Purpose:

- detect memory misuse during reproduction

Repository integration:

- ASan build script: [build-asan-msvc.cmd](F:\QtProject\DicomViewer\.vscode\build-asan-msvc.cmd:1)
- VS Code task: [tasks.json](F:\QtProject\DicomViewer\.vscode\tasks.json:1)
- VS Code launch profile: [launch.json](F:\QtProject\DicomViewer\.vscode\launch.json:1)
- CMake switch `MEDPRO_MSVC_ENABLE_ASAN` in [CMakeLists.txt](F:\QtProject\DicomViewer\CMakeLists.txt:1)

Operational notes:

- ASan build output is isolated under `build\asan\`
- currently not supported for the full application with the repository's default prebuilt dependency stack
- intended only as a future targeted workflow for local-only compatible binaries, or after rebuilding third-party libraries in an ASan-compatible way
- most useful for buffer overflows, use-after-free, and invalid memory access near the true fault point

What it helps locate:

- heap corruption
- stack corruption near writes
- lifetime bugs
- failures that manifest far away from the original bad write in normal runs

Current limitation:

- full-app MSVC ASan currently fails at link time because the application is instrumented but bundled third-party libraries are not
- observed failure class: `LNK2038` mismatches such as `annotate_string`
- impacted dependency chain includes prebuilt Qt/VTK/ITK/GDCM binaries
- therefore ASan should not currently be recommended as the first-line workflow for this repository

## In-process crash handling status

The project also includes an application-side crash handler:

- header: [crashhandler.h](F:\QtProject\DicomViewer\src\core\runtime\crashhandler.h:1)
- implementation: [crashhandler.cpp](F:\QtProject\DicomViewer\src\core\runtime\crashhandler.cpp:1)

Current responsibilities:

- install Qt message handler
- write session logs
- register Windows unhandled exception filter
- write application-managed minidumps
- install `std::terminate` handling

This layer complements WER rather than replacing it.

## Recommended debugging order

1. Reproduce with `RelWithDebInfo DicomViewerSkeleton`.
2. If it hard-crashes, inspect the dump with matching PDBs.
3. Read the latest Qt log to identify the last successful stage.
4. If the evidence suggests memory corruption, switch to `ASan Debug DicomViewerSkeleton`.
5. If the in-process dump is missing, verify WER LocalDumps is enabled and retry outside the debugger.

## Validation status from this integration round

Confirmed during this round:

- Debug build passed after integrating diagnostics code
- Qt file logging and in-process crash handling were compiled successfully
- VS Code tasks and launch profiles were added for `RelWithDebInfo` and `ASan`
- WER helper scripts and diagnostics documentation were added
- full-app ASan incompatibility was identified and documented as a third-party binary mismatch issue rather than an application source bug

Not fully validated in this round:

- full local execution of the WER scripts was not performed because they modify system registry state
- final `RelWithDebInfo` build verification was interrupted by a sandbox process-launch issue rather than a source-code compile error
- full-app `ASan` build is currently considered unsupported due to linker/runtime annotation mismatches with prebuilt dependencies

## Related document

See also: [crash-diagnostics.md](F:\QtProject\DicomViewer\docs\crash-diagnostics.md:1)
