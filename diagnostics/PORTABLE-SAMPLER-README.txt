RTX 3070 sampler validation

This package runs the SAME executable, shader and normalized query inputs used
on the RTX 5090. It does not change drivers or system settings. No project build
or Python installation is required. It uses Windows Direct3D 12 and the Microsoft
Visual C++ x64 runtime; if Windows reports a missing runtime DLL, send that error.

1. Extract the ZIP into a normal writable folder on your RTX 3070 laptop.
2. Plug the laptop in. In Windows Settings > System > Display > Graphics, select
   SamplerMicrobenchmark.exe and choose the high-performance NVIDIA GPU if needed.
3. Open PowerShell in the extracted folder and run:

   powershell -NoProfile -ExecutionPolicy Bypass -File .\Run.ps1

   ExecutionPolicy Bypass applies only to this PowerShell process.
4. Check the printed device name says NVIDIA GeForce RTX 3070 Laptop GPU
   (or the correct NVIDIA device), rather than the integrated GPU. If it does not,
   select the NVIDIA GPU in Windows graphics settings and rerun.
5. Return the results-TIMESTAMP.zip it creates, including all three sizes.
   Also mention whether you changed drivers between runs. No timing comparison
   is intended; power mode and laptop clocks are not the target of this test.

The test checks 23,503 coordinates at each of 32^3, 33^3 and 64^3, with 16 known
textures and three repeat dispatches. It saves the adapter/driver identity,
coordinates, corners, and samples. We will analyze the results back on the main
machine, checking invariants on the laptop independently before comparing GPUs.

Different numerical values are not automatically failures: the purpose is to
discover whether the RTX 5090's measured effective weights also describe the
RTX 3070. Agreement would extend evidence to these two tested configurations,
not establish a universal NVIDIA or Direct3D rule.
