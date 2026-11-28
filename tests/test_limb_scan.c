#undef NDEBUG
#include <assert.h>
#include "limb_scan.h"

typedef struct {
    limb_scan scan;
    int selected;
    unsigned held_lines[MO_SLOT_COUNT], body_lines, select_failures;
    unsigned selects, resets, polls[MO_SLOT_COUNT];
    bool lost_after_poll, mux_missing;
    enum limb_result node_result;
} fixture;

static bool blocked(const fixture *f)
{
    return f->body_lines || (f->selected >= 0 && f->held_lines[f->selected]);
}

static bool select_slot(void *ctx, unsigned slot)
{
    fixture *f = ctx;
    ++f->selects;
    if (blocked(f) || f->mux_missing) return false;
    if (f->select_failures) { --f->select_failures; return false; }
    f->selected = (int)slot;
    return true;
}

static bool recover(void *ctx)
{
    fixture *f = ctx;
    ++f->resets;
    f->selected = -1;
    return !blocked(f) && !f->mux_missing;
}

static bool poll(void *ctx, unsigned slot, limb_poll_result *out)
{
    fixture *f = ctx;
    ++f->polls[slot];
    assert(f->selected == (int)slot);
    if (f->lost_after_poll) f->body_lines = 1;
    if (blocked(f)) return false;
    *out = (limb_poll_result){.result=f->node_result};
    return true;
}

static enum scan_event step(fixture *f, unsigned slot, uint64_t now, limb_poll_result *out)
{
    const limb_scan_io io = {f, select_slot, recover, poll};
    return limb_scan_slot(&f->scan, &io, slot, now, out);
}

static void previous_branch_holds(unsigned lines)
{
    fixture f = {.selected=-1, .node_result=LIMB_NEW_SAMPLE};
    limb_poll_result out;
    assert(step(&f, 0, 100, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
    f.held_lines[0] = lines;
    assert(step(&f, 1, 100, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
    assert(f.resets == 1 && f.selects == 3 && f.polls[1] == 1);
    assert(!f.scan.slot_retry_ms[1] && !f.scan.bus_retry_ms);
    for (unsigned i=2; i<4; ++i)
        assert(step(&f, i, 100, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);

    assert(step(&f, 0, 150, &out) == SCAN_OBSERVED && out.result == LIMB_LINK_FAULT);
    assert(f.selected == -1 && !f.scan.bus_retry_ms);
    for (uint64_t now=200; now<650; now+=50) {
        assert(step(&f, 0, now, &out) == SCAN_SKIP);
        for (unsigned i=1; i<4; ++i)
            assert(step(&f, i, now, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
    }
    for (unsigned i=1; i<4; ++i) assert(f.polls[i] == 10);
    f.held_lines[0] = 0;
    assert(step(&f, 0, 650, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
}

static void public_fault(unsigned lines, bool missing)
{
    fixture f = {.selected=0, .body_lines=lines, .mux_missing=missing, .node_result=LIMB_NEW_SAMPLE};
    limb_poll_result out;
    assert(step(&f, 1, 100, &out) == SCAN_BUS_DOWN);
    assert(f.selected == -1 && !f.scan.slot_retry_ms[1] && !f.polls[1]);
    unsigned attempts = f.selects;
    for (unsigned i=0; i<4; ++i) assert(step(&f, i, 599, &out) == SCAN_SKIP);
    assert(f.selects == attempts && f.resets == 1);
    assert(step(&f, 0, 600, &out) == SCAN_SKIP && f.resets == 2); /* Same outage. */
    f.body_lines = 0;
    f.mux_missing = false;
    for (unsigned i=0; i<4; ++i)
        assert(step(&f, i, 1100, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
    assert(!f.scan.bus_retry_ms);
}

int main(void)
{
    previous_branch_holds(1);
    previous_branch_holds(2);
    public_fault(1, false);
    public_fault(2, false);
    public_fault(0, true);

    fixture f = {.selected=-1, .select_failures=2, .node_result=LIMB_NEW_SAMPLE};
    limb_poll_result out;
    assert(step(&f, 2, 0, &out) == SCAN_OBSERVED && out.result == LIMB_CHANNEL_FAULT);
    assert(f.resets == 2 && f.polls[2] == 0 && f.scan.slot_retry_ms[2] == 500);
    assert(step(&f, 3, 0, &out) == SCAN_OBSERVED && out.result == LIMB_NEW_SAMPLE);
    f.lost_after_poll = true; /* Upstream can also fail once a target was selected. */
    assert(step(&f, 0, 10, &out) == SCAN_BUS_DOWN && !f.scan.slot_retry_ms[0]);
    f.body_lines = 0;
    /* Isolated checks briefly pass, then the upstream fails again during I/O.
     * It is still the same outage until a slot observation can be delivered. */
    assert(step(&f, 0, 510, &out) == SCAN_SKIP);
    f.lost_after_poll = false;
    f.body_lines = 0;
    f.node_result = LIMB_FOREIGN_BINDING;
    assert(step(&f, 0, 1010, &out) == SCAN_OBSERVED && out.result == LIMB_FOREIGN_BINDING);
    assert(f.selected == -1 && f.scan.slot_retry_ms[0] == 2010);
    f.node_result = LIMB_BAD_FRAME;
    unsigned resets = f.resets;
    assert(step(&f, 1, 1010, &out) == SCAN_OBSERVED && out.result == LIMB_BAD_FRAME);
    assert(f.resets == resets && !f.scan.slot_retry_ms[1]);
    return 0;
}
