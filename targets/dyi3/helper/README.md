# dm1q-S911U1UES6DYI3 helper

Build: `make -C targets/dyi3/helper`. Output: `build/cve-2026-43499-root`.

This firmware has its own copy of `su_daemon.c`, build recipe, and artifact
directory. The payload and target checks remain in the firmware-specific
engine and runner.
