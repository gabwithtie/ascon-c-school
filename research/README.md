# Ascon v1.2 reproduction

The paper in `paper.tex` is a draft; complete its author information and
interpretation before submission. The measurements concern Ascon v1.2, not
NIST SP 800-232.

## Requirements and build

The provided presets target Windows x64, Visual Studio 2026 C++ tools, and
CMake 3.21 or newer. In VS Code CMake Tools, select `windows-v12` and build
`windows-v12-release`. Repeat with `windows-v12-size` and
`windows-v12-size-release`. The former uses MSVC `/O2`, the latter `/O1`;
each uses its own build directory. Run their Release tests serially.

Alternatively, from the repository root in PowerShell:

```powershell
cmake --preset windows-v12
cmake --build --preset windows-v12-release
ctest --preset windows-v12-release
cmake --preset windows-v12-size
cmake --build --preset windows-v12-size-release
ctest --preset windows-v12-size-release
```

The research targets are enabled by `ASCON_RESEARCH` in these presets; the
upstream project's default remains unchanged. The two fixed-vector tests use
entries 1 and 571 from the v1.2.8 KAT files. The same files are hosted by the
[lightweight-crypto project](https://github.com/rweather/lightweight-crypto/tree/fa4ec9a021b9233263de77e7dd971de8c9761495/test/kat);
their matching contents do not establish independent vector provenance.

## Independent implementation comparison

For stronger evidence, obtain the separately developed lightweight-crypto
implementation at commit `fa4ec9a021b9233263de77e7dd971de8c9761495`. Keep it
outside this repository and preserve its license. Set
`ASCON_INDEPENDENT_DIR` to its `src\individual\ASCON` directory in a separate
build. For example, after cloning and checking out that commit:

```powershell
cmake --preset windows-v12 -B build-independent `
  -DASCON_INDEPENDENT_DIR=C:\repos\lightweight-crypto\src\individual\ASCON
cmake --build build-independent --config Release
ctest --test-dir build-independent -C Release -R "^research_check_" -j 1 -V
```

Check that all four tests state that the independent comparison was enabled.
Each checks 130 message/AD combinations, successful decryption and rejection
of modified tags, ciphertext, associated data, nonce, and short ciphertext.
The comparison tests encryption outputs and independently decrypts them.
These checks do not prove security.

## Collecting measurements

Run the Release builds and tests first. The runner rechecks both test presets,
checks the configured flags, and refuses to overwrite an existing output
directory:

```powershell
.\research\run-experiments.ps1 -OutputDirectory .\research\results\my-run
```

It records 3 runs of 30 calibrated batches for each workload, algorithm,
implementation, and optimization setting. Calibration increases the batch size
until the shortest of three trial batches reaches 2 ms; measured batches can
still be shorter if the machine's speed changes.
The output consists of `raw.csv`, `summary.csv`, and `environment.json`.
The thread is pinned to the lowest processor allowed by its process affinity;
the affinity mask is recorded. Do not compare results collected under different
loads as if the environment were controlled. Reused deterministic key/nonce
pairs are for benchmarking only and must not be used in a real protocol.

Raw durations are Windows `QueryPerformanceCounter` ticks, **not CPU cycles**.
The overhead column is an adjacent pair of counter reads. It is recorded for
inspection and is not subtracted. Summaries convert duration to nanoseconds
per encryption and report pooled batch median and quartiles, as well as the
minimum and maximum of the three run medians. Batches within a run are not
independent experiments; pooled quartiles are descriptive, not confidence
intervals. Throughput divides plaintext bytes by median operation time, and
per-byte values are intentionally blank when the plaintext length is zero.

The paper uses `results\2026-11-18-v2`. That directory also contains the
independent test report and MSBuild compiler command records, captured
separately after the measurements. The earlier `results\2026-11-18` directory
is a pilot with a calibration flaw and is excluded from the paper.

The included implementation and data are experimental artifacts, not a
constant-time, side-channel, power, or microcontroller evaluation. Inspect the
environment and raw results, repeat the runs, and explain deviations before
including conclusions in a submission.
