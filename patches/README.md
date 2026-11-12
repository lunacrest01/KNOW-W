# Local ZMK patches

KNOB-W needs a small set of changes inside ZMK. The `zmk/` folder is ignored by
this repository, so those edits are archived here instead of living only in a
working copy.

`zmk-knob-w.patch` is generated against ZMK revision
`ead91ca094c6cd778574f73b9ae011ddab611c66` and contains:

- `app/src/ble.c` — per-connection pairing slot capture with generation
  invalidation, a filtered accept list built from saved hosts, explicit
  pairing-window advertising, advertising stability, immediate active-profile
  save, and the `knob_ble_diag()` counters.
- `app/include/zmk/knob_bond_policy.h` — the pure bonding rules and their
  compile-time assertions.
- `app/src/hog.c` — bounded HID queue retries instead of recursion, and HID
  descriptor version 1.11.
- `app/src/rgb_underglow.c` and its header — the `knob_rgb_override()` hook and
  absolute speed setting.

Apply after checking out the pinned ZMK revision:

    git -C zmk apply ../patches/zmk-knob-w.patch

Regenerate after editing the ZMK tree (from the project root):

    mkdir -p patches && cd zmk && { git diff; git diff --no-index -- /dev/null app/include/zmk/knob_bond_policy.h || true; } > ../patches/zmk-knob-w.patch

Verify a patch matches the current tree before committing it:

    git -C zmk apply --check -R ../patches/zmk-knob-w.patch
