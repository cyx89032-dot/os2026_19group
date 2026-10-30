# Lab2 移植、调试与 AI 协作记录（事后整理）

## 实际交互方式

操作者提供压缩包、目录列表、编译报错与 GDB 输出，持续请求下一步。AI 提供代码、解释、排错和文档帮助。此文件是摘要，不是逐字导出；具体模型版本和三人成员使用情况待确认。

## 移植约束

保留 Lab3 pmm.c 的 local_intr_save/local_intr_restore；只继承所需页初始化和 Best-Fit，不整体覆盖 kern。读取当前 memlayout.h 后，从 KERNBASE/KERNTOP 减去 PHYSICAL_MEMORY_OFFSET 求物理边界。

## 真实错误

最初遗漏 Lab2 依赖导致 free_list/nr_free 未声明；复制 page_init 时重复粘贴；Lab2 常量在 Lab3 中不存在。通过备份、生成适配文件、确认单一定义与三组中断保护、重新编译修复。最终评分100/100。

## 调试要求与结果

使用 riscv64-unknown-elf-gdb 连接 make debug，在 __alltraps、trap、__trapret、实际 sret 地址依次观察。保存入口 sp/epc/status，比较现场布局；计数0→1；返回后 PC/SP 恢复。所有相关比较为1。不得用宿主 QEMU MMU 调试替代这次客体 Trap 调试证据。

## 未完成事项

自动关机已于2026-10-30独立验证，最终评分100/100；自选阈值10仍需与教师规定核对；负责人注释、分工待确认；原始 prompts 目录为空，本次整理文件不能冒充预先设计并发送的历史提示词。
