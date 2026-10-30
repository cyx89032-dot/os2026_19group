# 中断与异常处理规格（事后整理）

本文件是本次实际实现和网页要求的事后整理，不是原始逐字对话。2026-10-30 已补充自动关机并回归验证，阈值采用自选的 10 次，课程指定值仍待核对。

## 接口

实现 void interrupt_handler(struct trapframe *tf) 和 void exception_handler(struct trapframe *tf)，不改变 trap_dispatch、trapframe 和 trap 接口。

## 已实现行为

1. 中断分支清除 cause 最高位后识别编号。
2. S 时钟中断调用 clock_set_next_event，ticks 恰好加一，每 TICK_NUM 次调用 print_ticks；不改变 epc。
3. S 软件中断输出提示，未知中断打印编号和现场。
4. 非法指令与断点异常输出规定前缀，在可读的16/32位内核代码范围内判断长度并推进 epc。
5. 其他异常打印 trapframe，不虚构完整恢复能力。

## 自动关机补充

原压缩包没有定义 SHUTDOWN_AFTER。补充实现采用可调整参数10，在每100 ticks输出后判断累计输出次数，达到10次打印统计信息并调用sbi_shutdown。独立运行确认10次输出、ticks=1000、自动退出码0。此参数不是已确认的课程规定。

## 验证

普通运行持续输出；非法指令/回退测试25分，时钟测试40分；GDB 检查 ticks 增量与 epc 不变。补充关机后重新评分100/100、退出码0；自动退出记录另存于evidence/run-lab3-shutdown.txt。
