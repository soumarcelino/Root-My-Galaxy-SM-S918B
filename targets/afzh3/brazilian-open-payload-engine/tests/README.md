# Focused diagnostics

This directory contains the small diagnostics still useful for isolating
production components. `tests/support/` contains implementations used only by
those diagnostics. None of these files is linked into the payload.

Build all diagnostics with:

```sh
make tests
```

Run the host-only regression for configfs AAR address encoding with
`make test-aar-read-plan`. It includes the three historical addresses whose
old 256-offset search returned `EOVERFLOW`.

`test_futex_trigger` touches live kernel state and requires the exact current
boot kernel base. The remaining tests keep their original scope and safety
notes in their source headers.

Superseded root-chain variants (`test_root*` and `test_root_v2` through
`test_root_v9`) were removed. Their reasoning and results remain in Git and
`STATUS.md`.
