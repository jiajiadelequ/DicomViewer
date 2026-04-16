# Crash Diagnostics

This project supports four complementary diagnostics layers on Windows:

1. `WER LocalDumps`
2. `Visual Studio / WinDbg + PDB`
3. `Qt file logging`
4. `AddressSanitizer`

## 1. WER LocalDumps

Enable OS-managed crash dumps with an elevated PowerShell session:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\enable-wer-localdumps.ps1
```

Disable it with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\disable-wer-localdumps.ps1
```

Default dump output folder:

```text
%LOCALAPPDATA%\DicomViewerSkeleton\wer-dumps
```

## 2. Visual Studio / WinDbg + PDB

Use the `RelWithDebInfo DicomViewerSkeleton` launch profile for optimized builds that still keep symbols.

Build output:

```text
build\relwithdebinfo\
```

When the app crashes:

1. Open the generated `.dmp` in Visual Studio or WinDbg.
2. Make sure the matching `.pdb` from the same build directory is available.
3. Inspect the exception code, call stack, loaded modules, and crashing thread.

## 3. Qt File Logging

Runtime Qt logs are written by the in-process diagnostics handler.

Default log folder:

```text
%LOCALAPPDATA%\DicomViewer\DicomViewerSkeleton\logs
```

The logs include:

- timestamp
- thread id
- log level
- logging category
- source file and line
- function name

## 4. AddressSanitizer

Status:

- experimental
- currently not usable for the full application in the default repository setup

Reason:

- this project links against prebuilt Qt, VTK, ITK, and GDCM binaries
- MSVC AddressSanitizer requires ABI-compatible instrumentation across the linked boundary for affected runtime annotations
- current full-app ASan builds fail with linker mismatches such as `LNK2038` (`annotate_string`) when mixed with the shipped third-party libraries

Do not treat the `build-asan` task or the `ASan Debug DicomViewerSkeleton` profile as a supported full-application workflow unless the third-party dependency stack has been rebuilt in a compatible way.

Build output:

```text
build\asan\
```

ASan is most useful for:

- heap buffer overflows
- stack buffer overflows
- use-after-free
- invalid memory access near the failure point

## Recommended workflow

1. Reproduce under `RelWithDebInfo` first.
2. If it hard-crashes, inspect the dump with matching PDBs.
3. If the crash looks memory-related, prefer dump analysis first; use ASan only if a local-only compatible target exists.
4. Use the Qt log to correlate the final successful operation before the crash.
