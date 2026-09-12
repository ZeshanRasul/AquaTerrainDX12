# Offline marker access control v1

User-authorized on 12 September 2026. Measurement transport only; pressure,
projection, numerical gates and rendering remain unchanged. Preserve all failed
roots and do not infer the historical OS error from a synthetic reproduction.

Replace the existence-check/unchecked-ifstream extraction with one native open
attempt per poll. Record Windows errors, elapsed time, expected step, accepted
publication and invalid bytes in each step's marker-access.txt. Retry only absent
publication and explicit sharing/locking conflicts, within the original 60-second
deadline. Other open errors and read failures stop immediately. Successfully read
contents must exactly match the publisher's decimal step format; never retry
wrong, empty or malformed contents. The worker still closes all output files
before atomically renaming response.tmp to response.ready.

Before coupled output, test delayed atomic publication, temporary exclusive lock,
wrong step, empty marker, malformed marker, missing-publication timeout and worker
rejection. Exercise the same C++ helper used by the renderer. Demonstrate whether
the legacy ifstream reader confuses a controlled lock with a wrong step.

Then run fresh 180-step 32³ A controls in both offline modes, applying full-field
rounding-aware validation. Require zero D3D12 errors. Stop on any failure. If both
pass, make the unchanged 26-run numerical matrix available for manual execution
in a fresh root; stop automatically on any execution, provenance or validation
failure. Opacity refinement remains a subsequent gate; this runner does not score
opacity or G1. The original handoff's OS cause remains unknown unless new evidence
identifies it.
