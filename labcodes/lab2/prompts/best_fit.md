# Best-Fit：需求与提示词整理

## Spec
- 使用 free_area_t 管理空闲链表和空闲页总数。
- 保留 free_list、nr_free 的状态约定，兼容已有测试。
- 空闲块按物理地址递增排列。
- 分配 n 页时，选择大小不小于 n 的最小空闲块。
- 大小相同时选择地址较低的块。
- 分配后将剩余部分重新加入空闲链表。
- 释放页面时按地址插入，并合并前后相邻空闲块。
- 正确维护 PageProperty、property、引用计数和空闲页数量。
- 定义 best_fit_pmm_manager，注册框架要求的全部接口。
- 不修改 basic_check() 和 best_fit_check()。

## Prompt
请参考 default_pmm.c、pmm.h、list.h 和 best_fit_pmm.c 的原有测试，
在指定位置实现完整 Best-Fit 分配器。
使用地址有序链表，分配时选择最小合适块，释放时合并相邻块。
保留框架测试，解释实现，并通过编译、运行及评分检查验证。

## 验证记录
已完成验证：Best-Fit 启动时 check_alloc_page() succeeded!，课程 check best fit 为 OK，总评分 20/20。见 evidence/run-best-fit.txt 与 evidence/grade-lab2-final.txt。原有 basic_check 和 best_fit_check 经与原始压缩包比较，忽略空白和注释后保持一致；管理器初始化接入错误及修复见报告。此段为报告整理时根据实际日志补录。
