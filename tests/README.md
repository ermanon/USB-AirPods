# Tests

## ARM recovery policies

From the repository root, with Python, the Pico ARM toolchain 14_2_Rel1 installed under the user's .pico-sdk directory and Pico SDK 2.1.1:

```powershell
python -m pip install --target build/test-runtime unicorn==2.1.4
python tests/run_arm_tests.py
```

Current result: 2515 checks, zero failures. The harness compiles production policies and unchanged extracted queue/reset/send, codec-selection and identity matching functions for ARM and runs them in Unicorn. Scheduling/stack operations are simulated. SDK Rijndael AES is used for Bluetooth address identity test vectors. Tests do not prove physical Bluetooth, USB, codec or audible behavior.

Optional negative controls, operating only on generated harness code:

```powershell
python tests/run_arm_tests.py queue-guard
python tests/run_arm_tests.py packet-frames
python tests/run_arm_tests.py
```

Negative controls must detect injected failures; the final normal run must pass.

## Battery and ANC C parsers

Windows with Visual Studio C++ Build Tools and Windows SDK:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test-firmware-parser.ps1
```

Covers packet bounds, incomplete updates, duplicate components, valid zero percent, unavailable values, disconnect clearing, USB nonce parsing and ANC session/observed-mode restrictions.

## Windows protocol

Windows .NET Framework 4.x:

```powershell
powershell -ExecutionPolicy Bypass -File windows-tray/test.ps1
```

Current result: 23 checks. See [VALIDATION.md](../VALIDATION.md) for separate user-confirmed hardware results and known limitations.
