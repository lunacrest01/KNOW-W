# Bongo Cat

Source: https://github.com/dancarroll/qmk-bongo

Pinned revision: cb60a2789960ca934254452848c4d1b6871f0cd4

Copyright 2021 github.com/dancarroll. Licensed GPL-2.0-or-later in
`bongo_cat.c`. The upstream GPL text is retained at
`third_party/qmk-bongo/LICENSE` together with the original source and credits.
Art is credited upstream to @pixelbenny, James Incandenza (github.com/j-inc),
and github.com/obosob.

KNOB-W extracts the two 512-byte tapping frames unchanged. The QMK rendering
functions are not used; the frames are drawn through Zephyr's monochrome CFB.
They play every 400 ms on the animation page and during the idle-animation stage.

When distributing this firmware commercially, retain the notices and meet the
applicable GPL corresponding-source requirements. Do not distribute it as a
closed-source-only firmware product without resolving those obligations.
Other included dependencies retain their own licenses.

## OLED font

Printable ASCII comes from Adafruit GFX's classic 5x7 bitmap font in
`third_party/adafruit-gfx/glcdfont.c`. Its BSD license is included in
`third_party/adafruit-gfx/license.txt`. Only glyph columns are embedded;
Zephyr CFB still handles drawing.
