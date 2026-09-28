# ZZI8 Open Payload Engine

Fresh target for `SM-S918B / S918BXXUAZZI8`, derived from the generic ZZHL engine. All firmware-bound values live in `src/target.h`.

Validation:

```sh
python3 tools/verify-zzi8-target.py
make test-aar-read-plan test-fops-layout test-compact-log
make so
```

The verifier checks 22 symbols, 33 BTF fields, and 5 structure sizes against the reconstructed ZZI8 kernel. Device execution is intentionally separate from build/install.
