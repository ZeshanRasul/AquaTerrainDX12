# Marker access findings and manual matrix run

12 September 2026. [Registered scope](MARKER_ACCESS_SPEC.md).

The historical ifstream code did not check opening/extraction before comparing
the default reply value (zero). An exclusive Windows file lock reproduces that
misdiagnosis despite correct `43` contents. This demonstrates a failure mechanism,
not proof that a lock caused the historical step-43 incident.

The replacement helper records native open/read errors separately from invalid
contents. The controlled lock produces Win32 32 (sharing violation), retries and
accepts after release. All seven transport controls passed: delayed atomic rename,
temporary lock, wrong step, empty marker, malformed marker, absent-marker timeout,
and worker rejection. Incorrect successfully read contents are never retried.
Only missing publication and explicit sharing/locking conflicts are retried within
the original deadline. Other access/read failures remain explicit blockers.

Release build passed. Fresh 180-step 32³ scene A runs in both pressure32 and
velocity64 passed the full-field validator: 360 steps, 360 accepted markers,
zero observed open failures, zero D3D12 errors/discarded messages. The same CPU/GPU
identity and independent divergence limits apply. Raw tests, sealed runs and
validation JSONs are in `runs/marker-access-v1/`. Frozen mass-audit and rendering
shader hashes are unchanged. The full matrix has not been launched.

## Run the matrix on this desktop

From the repository root in PowerShell:

```powershell
.\diagnostics\Run-FullReferenceControls.ps1
```

The rebuilt Release executable is ready. The wrapper uses the installed Codex
NumPy-enabled Python by default; `-Python <path>` overrides it if necessary. It
creates a fresh timestamped root under `experiments/appearance-attribution/runs/`
and prints its location. `-Root <new-directory>` overrides that location. Existing
roots are rejected; this does not resume or overwrite a failed matrix.

The fixed matrix is 12 runs at 180 steps, 12 at 360 steps, and two 128³ scene A
repeats. It validates every run, verifies independent trajectory prefixes and
repeats, pins source/build hashes, and stops at the first failure. This is offline
numerical validation, not a timing benchmark. Keep source/build files unchanged
and avoid running another Aqua experiment concurrently. The laptop replication
is not needed for this gate.

Return the root's `progress.json` and the adjacent `<root>.console.log`, whether
it passes or stops. Preserve all raw output locally for subsequent opacity
refinement; no need to zip the large field dataset initially. On execution failure,
the runner also copies application error/exit records into the root. A successful
numerical matrix still requires the registered rendered opacity refinement check
before G1; this command performs no opacity or cross-resolution scoring.
