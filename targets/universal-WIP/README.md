# Universal BOPE WIP

This is a separate BOPE target based on ZZI8. It loads the firmware symbol
offsets, BTF member offsets, static image base, address-space bounds, and
tracepoint fields from a runtime contract before any exploit attempt. No
firmware symbol address is compiled into the universal payload.

Run `../../tools/launch-universal-bope --check-only` to collect evidence,
generate and stage the contract, and ask the payload to validate it without
starting the exploit. Run `../../tools/launch-universal-bope` to execute through
the Stability Launcher after that validation. The scripts require a connected
ADB device and a working `/data/local/tmp/dirtyinit` relay.

`/tmp/universal/bope-contract-evidence.json` is the human-readable analysis
artifact. `target.contract` is the payload input: a versioned UTF-8 file with
one `key=value` entry per line. This format is easy to write from Python or
Kotlin and needs no JSON library in the native payload. The payload rejects
missing, duplicate, unknown, malformed, and mismatched identity fields. The
contract is tied to the current boot ID; it carries static offsets, while BOPE
still discovers the current boot's KASLR slide itself.

The ZZI8 allocator and timing calibration remains inherited in `target.h`.
The contract producer checks BTF structure sizes and relevant SLUB geometry
before creating the file. A different firmware still needs device validation
of the inherited calibration, the root helper, KernelSU late-load, and repeated
clean-reboot runs. This WIP target is not in the app profile or release path.
