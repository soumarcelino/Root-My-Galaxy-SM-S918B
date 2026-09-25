# AFZF5 payload

`cve-2026-43499-app.so` is the verified AFZF5 payload (MD5 `f6298194afb543d618b6f7015d1d08eb`). The Android app packages a copy at `app/src/main/assets/cve-2026-43499-app-afzf5.so`. The port script regenerates the payload under `build/` in this directory and checks its hash before staging it.

`src/` contains the target-local native source selected by the root `Makefile` for AFZF5.
