# Project Structure

This repository keeps source and reproduction material, not generated runtime
artifacts.

## Top-Level Layout

```text
app/
  Main Android application adapted to load local bundled assets through asset://.

app-src/
  Upstream app source snapshot kept for comparison and reference.

src/
  Native payload source snapshot.

src/targets/dm3q-S918BXXSAFZF5/
  Target headers for SM-S918B/S918BXXSAFZF5.

support/
  Upstream support feed examples for schema v2 and schema v3.

kernelsu/
  KernelSU notes, patch, and helper scripts.
  Prebuilt .ko and ksud binaries are not tracked.

tools/
  Porting, patching, and verification helpers.

Makefile
  Native payload build entry point from the payload source tree.

PORTING.md
  Upstream porting procedure used as a reference.

PROJECT-MANIFEST.txt
  Compact map of this local port and expected artifact checks.
```

## Most Relevant Files

```text
tools/port-sm-s918b-afzf5.sh
  Reproducible local port/build/staging script.

tools/port-sm-s918b-afzh3.sh
  Reproducible two-stage AFZH3 port, tuned-payload generation, APK build, and
  optional ADB staging script.

tools/f731u-to-dm3q-s918b-afzf5.spec.json
  Patch spec that changes the F731U base payload into the dm3q/S918BXXSAFZF5
  payload.

tools/f731u-to-dm3q-s918b-afzh3.spec.json
  Symbol/offset port from the F731U base to the laboratory AFZH3 firmware.

tools/f731u-to-dm3q-s918b-afzh3-tuned.spec.json
  Runtime tuning applied after the AFZH3 port; it preserves the validated
  exploit timing while removing redundant attempts and supervisor latency.

tools/patch_payload.py
  Applies the JSON patch spec to the base payload.

tools/generate_p0_fingerprint.pl
  Helper from the payload source tree for generating p0 fingerprint data.

tools/test_sigreturn_overlap.c
  Native test helper from the payload source tree.

kernelsu/patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch
  KernelSU reference patch used by the related payload source tree.

kernelsu/tools/audit_module_against_target.py
kernelsu/tools/extract_target_symvers.py
  Helper scripts for KernelSU module auditing and symbol extraction.

app/src/main/assets/support/targets-v2.json
  Bundled app profile for this exact target.
```

## Why There Are Two App Trees

`app/` is the practical app tree used by this port. It includes the local
`asset://` loading path and the bundled target profile.

`app-src/` is kept as an upstream reference snapshot. It is useful when
comparing behavior, UI, or repository history, but the reproduction script uses
the root-level `app/` tree.
