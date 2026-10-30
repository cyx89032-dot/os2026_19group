# Trap 入口与恢复规格（事后整理）

本文件根据 2026-10-27 实际对话、代码及测试整理，是可复用需求，不是实验开始前发送过的逐字 Prompt。原始过程是操作者逐次询问“下一步”，提供源码和输出，由 AI 给出实现并指导验证。

## 背景与目标

为 RV64 ucore 补全 kern/trap/trapentry.S 的 RESTORE_ALL 和 __alltraps，使当前内核态 Trap 能进入 C 函数并恢复执行。

## 已有接口与约束

- SAVE_ALL 已给出，不修改它和 trapframe 布局。
- 现场为 36 个 REGBYTES 槽位；通用寄存器位于 0..31，status/epc/badvaddr/cause 位于 32..35。
- 原 sp 在槽位 2；必须以该槽位恢复，并在其他栈读取完成后才恢复 sp。
- 恢复 sstatus、sepc，随后恢复通用寄存器，x0 无需恢复。
- a0 传递当前 sp 作为 struct trapframe *；调用 trap 后进入 __trapret，以 sret 返回。
- 不声称实现用户栈切换、浮点扩展上下文或多核嵌套异常支持。

## 验证

汇编编译通过；课程上下文测试通过；GDB 比较 tf 与原 sp-288、保存的 sp/epc/status，以及 sret 前后 PC/SP。

## 实际结果

课程上下文测试 35/35；GDB 对比均为 1。源码负责人标记需根据小组最终分工补填。
