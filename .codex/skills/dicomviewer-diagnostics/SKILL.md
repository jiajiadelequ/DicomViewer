---
name: dicomviewer-diagnostics
description: Diagnose crashes, instability, rendering failures, load failures, and memory issues in the DicomViewer Qt/VTK/ITK/GDCM project. Use when working inside this repository on bug investigation, crash triage, dump analysis preparation, logging review, WER setup, PDB-based debugging, AddressSanitizer runs, or when deciding which existing diagnostics capability should be used first.
---

# DicomViewer Diagnostics

Use the repository's existing diagnostics stack before inventing a new workflow.

## Start here

Read these project documents first:

- `docs/diagnostics-integration-log.md`
- `docs/crash-diagnostics.md`

Use them to determine:

- which diagnostics layers are already integrated
- where logs and dumps are written
- which VS Code task or launch profile matches the failure mode
- what has already been validated versus only wired up

## Preferred triage order

1. Reproduce with `RelWithDebInfo` when the issue appears release-like or timing-sensitive.
2. If the process hard-crashes, look for a dump first.
3. Correlate the crash with the latest Qt file log.
4. If the failure suggests memory corruption, switch to the ASan build.
5. If no in-process dump appears, check whether WER LocalDumps should be enabled.

## Existing project capabilities

Assume these capabilities exist unless the repository docs say otherwise:

- in-process Qt file logging
- in-process Windows crash dump generation
- WER LocalDumps helper scripts
- `RelWithDebInfo` build/debug workflow
- ASan-specific build/debug workflow

Do not re-add parallel diagnostics mechanisms unless there is a clear gap in the current stack.

## When investigating a problem

First classify the failure:

- `hard crash`: process exits unexpectedly, access violation, abort, or dump-producing failure
- `soft failure`: load fails, rendering is wrong, warning dialog appears, but process survives
- `memory suspicion`: use-after-free, random crash site, inconsistent repro, heap corruption symptoms
- `environment/config`: symbols missing, dumps missing, wrong runtime DLLs, debugger-only behavior

Then use the matching workflow:

### Hard crash

- check the latest dump output
- verify matching PDBs from the same build
- inspect the crashing thread, exception code, and top stack frames
- read the last Qt log lines before the crash

### Soft failure

- read the latest Qt/session log first
- inspect the relevant load or rendering path in source
- prefer adding focused category logging over broad noisy logging

### Memory suspicion

- use the ASan task/profile
- prefer reproducing with the smallest dataset or action sequence that still fails
- treat the first invalid access report as more trustworthy than the final crash site in a non-ASan run

### Environment/config

- verify whether the run used `Debug`, `RelWithDebInfo`, `Release`, or `ASan`
- verify symbol presence before deep dump analysis
- verify WER is enabled only when needed and with the expected dump folder

## Repository entry points

Expect the main diagnostics entry points here:

- `main.cpp`
- `src/core/runtime/crashhandler.h`
- `src/core/runtime/crashhandler.cpp`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `tools/enable-wer-localdumps.ps1`
- `tools/disable-wer-localdumps.ps1`

## Working rules

- preserve existing diagnostics behavior unless the task explicitly asks to replace it
- prefer extending current logging categories and crash handling rather than adding a second system
- keep diagnostics additions cheap in normal runs and more expensive only in dedicated debug/ASan workflows
- update `docs/diagnostics-integration-log.md` whenever diagnostics capability changes

## Completion checklist

Before finishing diagnostics-related work:

- confirm whether the issue was investigated via log, dump, symbols, ASan, or configuration review
- record any new capability in `docs/diagnostics-integration-log.md`
- record any user-facing usage change in `docs/crash-diagnostics.md`
