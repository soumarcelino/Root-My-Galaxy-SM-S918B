# Universal BOPE payload (WIP)

`src/runtime_keys.def` lists the 100 numeric fields loaded before the first
attempt. `src/runtime_contract.c` parses `BOPE_TARGET_CONTRACT` and checks exact
model, device, incremental build, fingerprint, kernel release/version, and boot
ID. `src/target.h` retains the inherited ZZI8 object geometry and calibrated
timing while the address and ABI field macros resolve through the loaded values.

The repository-level `tools/launch-universal-bope` collects and verifies the
boot-derived contract, builds this payload, stages it, and launches it through
the Stability Launcher. `--check-only` performs the full contract preflight
without entering the exploit.
