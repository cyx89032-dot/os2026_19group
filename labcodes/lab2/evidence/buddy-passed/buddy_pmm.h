#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__
#include <pmm.h>
#define BUDDY_MAX_ORDER 15
extern const struct pmm_manager buddy_pmm_manager;
void buddy_pmm_check(void);
#endif
