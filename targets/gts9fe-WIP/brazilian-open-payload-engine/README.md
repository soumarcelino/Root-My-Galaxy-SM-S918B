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

## Reclaim occupancy: measured, not gated

The fake `file_operations` table is installed into a page carved from
`skbuff_head_cache`. Device measurements on SM-X518U show that every observed
success had a completely full cache while every failure had free objects:

| run | skb active/total | free | outcome |
| --- | --- | --- | --- |
| run | `1312/1312` | 0 | success |
| run7 | `1120/1120` | 0 | success |
| gate3 | `1318/1344` (98%) | 26 | failed at verify |
| run8 | `1407/1856` | 449 | failed at verify |
| run6 | `803/1088` | 285 | failed at verify |
| run4 | `1414/1728` | 314 | failed at verify |
| run5 | `761/1312` | 551 | failed at verify |

A gate that requires a full cache was implemented, measured and **reverted**.
It is not usable as a control: on a real device state the cache sat at
`953/1120` (167 free objects) and never filled across three attempts, so the
gate rejected every attempt and turned a working exploit into a 0% success
rate. "Full" is an outcome of the reclaim that cannot be forced by waiting.

The underlying asymmetry is still real: a failed verify happens after the
mutation boundary, so the supervisor refuses to retry it and the run ends in a
required reboot. Three configured attempts therefore collapse to one. Fixing
that reliability properly needs the fake fops page to be provably exclusive
before the trigger, which the slab counters alone cannot establish.
