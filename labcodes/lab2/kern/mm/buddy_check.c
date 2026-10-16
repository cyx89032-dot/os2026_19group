#include <buddy_pmm.h>
#include <assert.h>
#include <stdio.h>

/* Tests use the manager interface, not its private free lists. */
/* This 32 KiB test bitmap is part of the kernel's reserved BSS. */
static unsigned char owned[1U << BUDDY_MAX_ORDER];

static size_t rounded(size_t n) {
    size_t result = 1;
    while (result < n) result *= 2;
    return result;
}

void buddy_pmm_check(void) {
    const struct pmm_manager *m = &buddy_pmm_manager;
    size_t initial = m->nr_free_pages();
    size_t descriptors = npage - nbase;
    assert(descriptors <= sizeof(owned));
    assert(initial >= 128);
    const size_t requests[] = {1, 3, 5, 8, 17};
    struct Page *allocated[5];
    size_t used = 0;
    for (size_t i = 0; i < 5; i++) {
        allocated[i] = m->alloc_pages(requests[i]);
        assert(allocated[i] != NULL);
        size_t size = rounded(requests[i]);
        size_t ppn = page2ppn(allocated[i]);
        assert((ppn & (size - 1)) == 0);
        assert(ppn >= nbase && size <= npage - ppn);
        for (size_t j = 0; j < size; j++) assert(!PageReserved(allocated[i] + j));
        for (size_t j = 0; j < i; j++) {
            size_t prev = page2ppn(allocated[j]);
            assert(ppn + size <= prev || prev + rounded(requests[j]) <= ppn);
        }
        used += size;
        assert(m->nr_free_pages() == initial - used);
    }
    const unsigned release_order[] = {2, 0, 4, 1, 3};
    for (size_t i = 0; i < 5; i++) {
        unsigned j = release_order[i];
        m->free_pages(allocated[j], requests[j]);
        used -= rounded(requests[j]);
        assert(m->nr_free_pages() == initial - used);
    }
    assert(m->alloc_pages(((size_t)1 << BUDDY_MAX_ORDER) + 1) == NULL);
    assert(m->nr_free_pages() == initial);
    cprintf("buddy check: rounding, alignment, non-overlap and accounting OK\n");

    for (size_t i = 0; i < descriptors; i++) owned[i] = 0;
    size_t count = 0;
    struct Page *p;
    while ((p = m->alloc_pages(1)) != NULL) {
        size_t index = p - pages;
        assert(index < descriptors && !owned[index] && !PageReserved(p));
        owned[index] = 1;
        count++;
        assert(count <= initial);
        assert(m->nr_free_pages() == initial - count);
    }
    assert(count == initial && m->nr_free_pages() == 0);
    assert(m->alloc_pages(1) == NULL && m->alloc_pages(2) == NULL);
    cprintf("buddy check: full exhaustion and allocation failure OK\n");

    /* With everything allocated, release exactly one aligned 8-page range.
       Allocating 8 can now succeed only if three levels of merging work. */
    size_t group = descriptors;
    for (size_t i = 0; i + 8 <= descriptors; i++) {
        if (((nbase + i) & 7) != 0) continue;
        size_t j = 0;
        while (j < 8 && owned[i + j]) j++;
        if (j == 8) { group = i; break; }
    }
    assert(group != descriptors);
    const unsigned order[] = {0, 2, 4, 6, 1, 3, 5, 7};
    for (unsigned i = 0; i < 8; i++) {
        unsigned j = order[i];
        m->free_pages(pages + group + j, 1);
        owned[group + j] = 0;
    }
    assert(m->nr_free_pages() == 8);
    p = m->alloc_pages(8);
    assert(p == pages + group && m->nr_free_pages() == 0);
    m->free_pages(p, 8);
    for (unsigned i = 0; i < 8; i++) {
        p = m->alloc_pages(1);
        assert(p != NULL);
        size_t index = p - pages;
        assert(index >= group && index < group + 8 && !owned[index]);
        owned[index] = 1;
    }
    assert(m->nr_free_pages() == 0);
    cprintf("buddy check: multi-level merge and split OK\n");
    for (size_t i = descriptors; i > 0; i--) {
        if (owned[i - 1]) {
            m->free_pages(pages + i - 1, 1);
            owned[i - 1] = 0;
        }
    }
    assert(m->nr_free_pages() == initial);
    cprintf("buddy check: all pages restored OK\n");
}
