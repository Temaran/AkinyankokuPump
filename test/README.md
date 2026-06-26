# Garden Pump Tests

Run the host-side regression suite from the repo root:

```powershell
.venv\Scripts\python.exe test\run_tests.py
```

The suite uses only Python's standard library. It covers pure firmware rules in
`src/GardenLogic.h` and source-level web UI checks for the embedded dashboard.

PlatformIO or board-in-the-loop tests can be added later, but this runner is the
minimum gate to run before changing firmware behavior.
