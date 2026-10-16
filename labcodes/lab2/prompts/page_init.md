# page_init：需求与提示词整理

## Spec
- 从设备树读取物理内存基址和大小，检查空范围及地址加法溢出。
- 设置 va_pa_offset，按框架可管理上界限制内存范围。
- npage 表示管理范围末端的物理页号上界，不是可用页数量。
- pages 对应从 nbase 开始的物理页描述符数组。
- 描述符数组放在内核 end 之后并按页对齐。
- 数组占用空间必须位于有效内存范围内。
- 所有描述符先初始化为保留状态。
- 仅将排除固件、内核和描述符数组后的完整页面交给 init_memmap。
- 没有完整空闲页时不得调用 init_memmap。
- 无效或本框架无法支持的内存布局应 panic。

## Prompt
请结合本项目的 pmm.c、pmm.h、memlayout.h、dtb.h 和链接脚本，
实现 static void page_init(void)。
使用现有地址转换、页面状态和对齐宏，不修改框架测试。
注意区分虚拟地址、物理地址、物理页号和 Page 描述符地址。
解释实现，并通过编译检查和后续运行测试验证。

## 验证记录
已完成验证：课程评分中 check page init 为 OK。完整结果见 evidence/grade-lab2-final.txt。调试时曾误将 pmm_init 内的 init_pmm_manager() 调用替换成管理器赋值，导致初始化链中断；恢复该调用，并在 init_pmm_manager 内选择管理器后，启动及自检通过。此段为报告整理时根据实际日志补录。
