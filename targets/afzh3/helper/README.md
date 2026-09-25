# dm3q-S918BXXSAFZH3 helper

Build: `make -C targets/afzh3/helper`. Output: `build/cve-2026-43499-root`.

This firmware has its own copy of `su_daemon.c`, build recipe, and artifact
directory. The payload and target checks remain in the firmware-specific
engine and runner.
