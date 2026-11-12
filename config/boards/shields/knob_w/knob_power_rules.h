#pragma once
#define KNOB_IDLE_STAGE(elapsed_ms, minutes) \
    ((elapsed_ms) >= ((minutes) + 5) * 60000LL ? 2 : ((elapsed_ms) >= (minutes) * 60000LL ? 1 : 0))
#define KNOB_LOW_BATTERY(percent) ((percent) >= 0 && (percent) < 20)

_Static_assert(KNOB_IDLE_STAGE(599999,10)==0,"Stay active before ten minutes");
_Static_assert(KNOB_IDLE_STAGE(600000,10)==1,"Show Bongo at ten minutes");
_Static_assert(KNOB_IDLE_STAGE(899999,10)==1,"Keep Bongo until fifteen minutes");
_Static_assert(KNOB_IDLE_STAGE(900000,10)==2,"Blank OLED at fifteen minutes");
_Static_assert(!KNOB_LOW_BATTERY(-1),"Unknown battery must not be treated as empty");
_Static_assert(KNOB_LOW_BATTERY(0) && KNOB_LOW_BATTERY(19),"Low battery disables RGB");
_Static_assert(!KNOB_LOW_BATTERY(20),"Twenty percent is not below twenty percent");
