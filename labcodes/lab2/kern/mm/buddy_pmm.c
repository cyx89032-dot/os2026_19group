#include <buddy_pmm.h>
#include <assert.h>
#include <list.h>

/* Lab2 Buddy extension. Fill in the actual contributor in the report. */
/* Free-head property = order. Allocated-head property = original request. */
#define BUDDY_BUSY (1UL << 2)
#define BUDDY_HEAD (1UL << 3)
static list_entry_t free_lists[BUDDY_MAX_ORDER + 1];
static size_t free_count;

static size_t block_size(unsigned order) { return (size_t)1 << order; }

static unsigned request_order(size_t n) {
    unsigned order = 0;
    while (block_size(order) < n) order++;
    return order;
}

static void insert_block(struct Page *p, unsigned order) {
    p->property = order;
    SetPageProperty(p);
    list_add(&free_lists[order], &p->page_link);
}

static void remove_block(struct Page *p) {
    list_del_init(&p->page_link);
    ClearPageProperty(p);
    p->property = 0;
}

static void release_block(struct Page *p, unsigned order) {
    while (order < BUDDY_MAX_ORDER) {
        size_t ppn = page2ppn(p);
        size_t other_ppn = ppn ^ block_size(order);
        size_t size = block_size(order);
        if (other_ppn < nbase || other_ppn >= npage ||
            size > npage - other_ppn) break;
        struct Page *other = pages + (other_ppn - nbase);
        if (!PageProperty(other) || other->property != order ||
            PageReserved(other) || (other->flags & BUDDY_BUSY)) break;
        remove_block(other);
        if (other_ppn < ppn) p = other;
        order++;
    }
    insert_block(p, order);
}

static void buddy_init(void) {
    for (unsigned i = 0; i <= BUDDY_MAX_ORDER; i++) list_init(&free_lists[i]);
    free_count = 0;
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    size_t start = base - pages;
    assert(start < npage - nbase && n <= npage - nbase - start);
    for (size_t i = 0; i < n; i++) {
        struct Page *p = base + i;
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }
    free_count += n;
    /* Partition arbitrary endpoints into aligned power-of-two blocks. */
    while (n != 0) {
        unsigned order = 0;
        while (order < BUDDY_MAX_ORDER && block_size(order + 1) <= n &&
               (page2ppn(base) & (block_size(order + 1) - 1)) == 0) order++;
        size_t size = block_size(order);
        release_block(base, order);
        base += size;
        n -= size;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > block_size(BUDDY_MAX_ORDER)) return NULL;
    unsigned wanted = request_order(n), order = wanted;
    size_t size = block_size(wanted);
    if (size > free_count) return NULL;
    while (order <= BUDDY_MAX_ORDER && list_empty(&free_lists[order])) order++;
    if (order > BUDDY_MAX_ORDER) return NULL;
    struct Page *p = le2page(list_next(&free_lists[order]), page_link);
    remove_block(p);
    while (order > wanted) {
        order--;
        insert_block(p + block_size(order), order);
    }
    for (size_t i = 0; i < size; i++) {
        assert(!PageReserved(p + i) && !PageProperty(p + i));
        (p + i)->flags = BUDDY_BUSY;
        (p + i)->property = 0;
        set_page_ref(p + i, 0);
    }
    p->flags |= BUDDY_HEAD;
    p->property = n;
    free_count -= size;
    return p;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0 && n <= block_size(BUDDY_MAX_ORDER));
    size_t start = base - pages;
    assert(start < npage - nbase);
    assert((base->flags & BUDDY_HEAD) && base->property == n);
    unsigned order = request_order(n);
    size_t size = block_size(order);
    assert(size <= npage - nbase - start);
    assert((page2ppn(base) & (size - 1)) == 0);
    for (size_t i = 0; i < size; i++) {
        struct Page *p = base + i;
        assert((p->flags & BUDDY_BUSY) && !PageReserved(p) && !PageProperty(p));
        assert(i == 0 || !(p->flags & BUDDY_HEAD));
        assert(page_ref(p) == 0);
    }
    for (size_t i = 0; i < size; i++) {
        (base + i)->flags = 0;
        (base + i)->property = 0;
        list_init(&(base + i)->page_link);
    }
    free_count += size;
    release_block(base, order);
}

static size_t buddy_nr_free_pages(void) { return free_count; }

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_pmm_check,
};
