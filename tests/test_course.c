#undef NDEBUG
#include <assert.h>
#include "moban_core.h"

int main(void)
{
    mo_init(42);
    assert(mo_read(42,0,0,0)==MO_EMPTY);
    assert(mo_presence(0,1)==MO_OK && mo_presence(1,1)==MO_OK);
    assert(mo_sample(42,0,1,100,0,0)==MO_OK);
    assert(mo_read(42,0,1,2600)==MO_OK && mo_read(42,0,1,2601)==MO_NO_FRESH_DATA);
    assert(mo_start(42,0,1,200,0)==MO_OK);
    assert(mo_start(42,0,1,200,0)==MO_COURSE_CHANGED);
    assert(mo_sample(42,0,1,1000,2123,6000)==MO_OK);
    assert(mo_sample(42,0,1,1000,2123,6000)==MO_NO_FRESH_DATA);
    assert(mo_presence(0,0)==MO_OK && mo_course()->phase==MO_WAITING);
    assert(mo_presence(0,1)==MO_OK);
    assert(mo_sample(42,0,1,2000,2222,6000)==MO_OLD_TARGET);
    assert(mo_sample(42,1,1,2000,2200,5000)==MO_OK);
    assert(mo_resume(42,1,1,2000,1)==MO_WRONG_PHASE);
    assert(mo_sample(42,0,3,2100,2200,5000)==MO_OK);
    assert(mo_resume(42,0,3,2100,1)==MO_OK);
    for (unsigned k=0;k<9;k++) assert(mo_sample(42,0,3,3100u+1000u*k,2230,5310)==MO_OK);
    assert(mo_course()->phase==MO_DONE && mo_course()->count==10 && mo_course()->temp_centi[0]==2123);
    mo_init(43);
    assert(mo_read(42,0,0,0)==MO_OLD_BOOT);
    return 0;
}
