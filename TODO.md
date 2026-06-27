# Garden Pump TODO

## Before outdoor deployment

- Reapply and hardware-verify crash recovery incrementally:
  - retained crash-stage instrumentation
  - watchdog recovery after startup is complete
  - safe relay state during recovery
- Send a forced Google Sheet test row over Serial while no sensors are attached, and confirm it appears in the sheet.
- Exercise the WebUI Stats graph repeatedly while irrigation animation and cloud/network activity are running; confirm the graph loads and the board remains responsive.
