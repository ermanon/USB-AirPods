# v1.1.0 validation record

Date: 2026-11-01. Hardware: Pico 2 W and AirPods 4 ANC. Firmware marker: case-battery-anc-v1.

## Passed checks

- Firmware compilation with Pico SDK 2.1.1 / Arm GNU 14.2.Rel1.
- 2,515 ARM checks covering audio recovery, queue/IRQ ownership, deadlines, packet bounds, case/identity policies, and codec negotiation.
- Native battery/ANC parser tests covering truncated/malformed notifications, partial updates, invalid values, disconnect clearing, query nonces, and ANC session/mode restrictions.
- 23 Windows protocol checks.
- Pico flash loading and picotool verification: OK.
- Battery, ANC, and device responses to real USB queries.
- Queued acknowledgments for ANC commands, followed by separate headphone notifications confirming noise cancellation and then Off.
- User confirmation: battery percentages and ANC mode are visible; selecting Off from the app works; both earbuds and the microphone work cleanly.

## Limitations

Missing case battery data was correctly displayed as unknown. ANC controls are limited to modes reported by the headphones during the current connection. An earlier case-return test showed unilateral silence and approximately 15-second automatic recovery; no permanent fix is claimed. Repeated range/case cycles and long-duration stability were not retested for the new battery/ANC integration.

Raw logs, device addresses, and older experimental firmware are excluded from this clean release. The app may write a size-limited companion-status.log and one rotated copy beside its EXE for local diagnostics; those files are not included in Git.
