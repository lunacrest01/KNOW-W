#include <assert.h>
#include <zmk/knob_bond_policy.h>

int main(void) {
    for (int known=-1;known<3;known++)
        for (int selected=0;selected<3;selected++)
            for (int open=0;open<2;open++)
                for (int pairing=0;pairing<2;pairing++) {
                    int slot=KNOB_BOND_SLOT(known,selected,open,pairing);
                    assert(slot==(known>=0?known:(open&&pairing?selected:-1)));
                    if (known>=0) { assert(slot==known); }
                }
    assert(KNOB_BOND_VALID(1,-1,1,0,3,3));
    assert(!KNOB_BOND_VALID(1,-1,1,0,3,4));
    assert(!KNOB_BOND_VALID(1,0,1,0,3,3));
    assert(KNOB_BOND_VALID(0,0,0,1,7,7));
    assert(!KNOB_BOND_VALID(0,0,0,0,7,7));
    assert(!KNOB_BOND_VALID(-1,-1,1,0,0,0));
    /* Another slot's reset does not invalidate a captured pairing target. */
    unsigned generations[3]={4,7,9};
    unsigned saved=generations[1];
    generations[0]++;
    assert(KNOB_BOND_VALID(1,-1,1,0,saved,generations[1]));
    generations[1]++;
    assert(!KNOB_BOND_VALID(1,-1,1,0,saved,generations[1]));
    return 0;
}
