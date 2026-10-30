# 操作系统实验报告

## 一、实验基本信息与完成范围

| 项目 | 内容 |
|---|---|
| 实验名称 | Lab3：断，都可以断——RISC-V 中断与异常处理 |
| 小组 | 第 19 组 |
| 成员 | 2412055 慈贻翔；2411861 王春晓；2414051 张玉鑫 |
| 实验与调试日期 | 2026-10-27 |
| 实验环境 | WSL Ubuntu-24.04、QEMU virt、RV64 |
| 仓库 | https://github.com/cyx89032-dot/os2026_19group |
| 报告状态 | 基于当前通过测试的代码整理；分工与下列提交前事项待确认 |

### 1.1 小组分工

截至本报告整理时，三人具体分工尚未确定，不沿用 Lab1 或 Lab2 的分工，也不根据终端用户名推定所有成员的个人贡献。

| 成员 | Lab3 实际承担工作 |
|---|---|
| 慈贻翔（2412055） | 待小组确认 |
| 王春晓（2411861） | 待小组确认 |
| 张玉鑫（2414051） | 待小组确认 |

### 1.2 已完成与待完成事项

本报告以 `lab3-current.tar.gz` 中的代码和证据为准。课程评分为 100/100，用户终端显示评分命令退出码为 0；这证明提供的测试通过，不等于所有文字要求都已覆盖。

| 项目 | 状态与证据 |
|---|---|
| 继承 Lab2 页初始化与 Best-Fit | 已完成，完整编译通过，默认 First-Fit 启动自检通过 |
| RESTORE_ALL 与汇编到 C 的调用 | 已实现；寄存器上下文及断点返回测试 35/35 |
| 非法指令与断点异常 | 已实现；非法指令及回退分支测试 25/25 |
| 持续时钟中断 | 已实现；时钟测试 40/40，普通运行反复输出 100 ticks |
| GDB 时钟中断完整流程 | 已验证入口、保存、处理、恢复与 sret 返回 |
| 达到规定输出次数后自动关机 | **当前未实现，待核对框架版本与阈值后补充** |
| lab3 代码注释中的实际负责人学号 | **当前标记已在编辑中被替换，提交前需补回并据实填写** |
| 小组分工 | 待确认 |

2026-10-27 核对的[练习页面](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_2_1_exercise.html)要求时钟累计输出达到 `SHUTDOWN_AFTER` 后调用关机接口。但本次提供的源码没有定义 `SHUTDOWN_AFTER`，当前处理函数也没有调用 `sbi_shutdown()`。运行记录中的 `QEMU: Terminated` 是手动退出，不是自动关机成功证据。不能自行猜测课程规定的阈值，也不能把评分满分写成这一项已完成。

## 二、实验环境与本章总体逻辑

### 2.1 工具版本

| 工具 | 版本 |
|---|---|
| riscv64-unknown-elf-gcc | SiFive GCC 10.1.0 |
| riscv64-unknown-elf-gdb | SiFive GDB 9.1 |
| QEMU | 4.1.1 |
| Python | 3.12.3 |
| 内存 | DTB 报告 128 MiB，范围 `[0x80000000, 0x88000000)` |

证据：[工具版本](evidence/versions.txt)。本次使用 RISC-V GDB 连接 QEMU 的客体调试端口，调试的是 ucore 内核；不同于 Lab2 使用宿主 GDB 调试 QEMU 自身的 MMU 实现。

### 2.2 从启动到中断返回

Lab1 使内核能够启动，Lab2 建立物理页管理基础，Lab3 使内核能够在发生事件时保存执行现场、处理事件并恢复执行。完整流程如下：

```text
kern_init
  → 初始化控制台与物理内存
  → idt_init：设置 sscratch、stvec
  → clock_init：允许 S 态时钟中断，设置首次定时事件，ticks 清零
  → intr_enable：开启 S 态中断总开关
  → 内核等待循环

定时事件到来
  → 硬件记录 sepc/scause 等 Trap 状态，调整中断状态并转入 stvec
  → __alltraps / SAVE_ALL：在栈上保存 Trap frame
  → a0 = sp；jal trap
  → trap_dispatch → interrupt_handler
  → 设置下次定时事件、ticks 加一、必要时输出
  → __trapret / RESTORE_ALL：恢复 CSR 与通用寄存器
  → sret：返回被打断的位置
```

硬件不会自动替软件把全部通用寄存器压入内核栈；保存这些寄存器是 `SAVE_ALL` 的工作。`idt_init` 是框架沿用的函数名，在本 RISC-V 实验中实际写入的是 `stvec`，不是建立 x86 IDT。源码注释中的 `iret` 也是历史用语，实际返回指令是 `sret`。

### 2.3 功能与文件的对应关系

| 文件 | 作用 |
|---|---|
| [trapentry.S](kern/trap/trapentry.S) | 保存现场、传递参数、恢复现场和返回 |
| [trap.h](kern/trap/trap.h) | 定义 pushregs、trapframe 和接口 |
| [trap.c](kern/trap/trap.c) | 设置入口、分派 Trap、处理中断和异常 |
| [clock.c](kern/driver/clock.c) | 读取时间、设置定时事件和初始化 ticks |
| [intr.c](kern/driver/intr.c) | 控制 S 态中断总开关 |
| [sync.h](kern/sync/sync.h) | 保存并恢复本地中断状态 |
| [pmm.c](kern/mm/pmm.c) | 页初始化、物理页分配接口及中断保护 |
| [sbi.c](libs/sbi.c) | 通过 SBI 请求固件提供定时等服务 |

## 三、Lab2 代码继承与内存初始化

### 3.1 为什么 Lab3 仍依赖物理内存管理

Lab3 的初始化流程仍调用 `pmm_init()`。原始框架在 `best_fit_pmm.c` 和 `pmm.c` 留出了 Lab2 内容，因此只有补全 Trap 代码还不能完成编译。

本次复制之前通过测试的 Best-Fit 文件，并将 `page_init()` 适配到 Lab3。默认管理器保留为 `default_pmm_manager`，因此本次普通启动运行的是 First-Fit 自检，不把它写成 Best-Fit 已在 Lab3 中动态复测。

### 3.2 page_init 的职责与常量适配

`page_init()` 从 DTB 获取内存范围，检查边界，确定可管理物理页上限，在内核映像之后安排 `struct Page` 数组，先将描述的页标记为保留，再将剩余完整页交给 `init_memmap()` 登记为空闲页。

Lab3 没有定义先前实现使用的 `KERNEL_BEGIN_PADDR` 和 `PHYSICAL_MEMORY_END`，所以改用当前框架已有宏计算：

```c
const uintptr_t kernel_begin_pa =
    (uintptr_t)KERNBASE - (uintptr_t)PHYSICAL_MEMORY_OFFSET;
const uintptr_t physical_memory_end =
    (uintptr_t)KERNTOP - (uintptr_t)PHYSICAL_MEMORY_OFFSET;
```

本框架对应内核起始物理地址 `0x80200000` 和管理上限 `0x88000000`。DTB 范围与框架可支持范围共同决定实际管理上限，不能把设备报告的全部地址都无条件当作可分配内存。

### 3.3 不能整体覆盖 pmm.c 的原因

Lab3 的 `alloc_pages`、`free_pages`、`nr_free_pages` 都包含 `local_intr_save` 和 `local_intr_restore`。它们在操作管理器之前保存当前中断状态并关闭本地中断，完成后恢复原状态，减少同一 CPU 上中断处理与普通代码交错操作共享结构的风险。

这不是多核互斥锁，也不能自动阻止其他 CPU 同时访问；更不能用“始终打开中断”代替“恢复原来的中断状态”。本次修正保留了三组保护，没有用 Lab2 的整个 pmm.c 覆盖它们。

实际普通启动显示可用区间 `[0x80348000, 0x88000000)`，空闲页 31928 页，并输出 `check_alloc_page() succeeded!`。页数与当前内核大小、Page 元数据大小有关，不应作为其他构建的固定值。

## 四、模块一：Trap 现场恢复与入口

### 4.1 功能规格 Spec

输入为当前栈上的 `struct trapframe`；输出为恢复后的通用寄存器、用于返回的状态寄存器和执行位置。实现必须与既有 `SAVE_ALL` 和 `trapframe` 布局一致，不改变结构体布局或 `trap(struct trapframe *tf)` 接口。

保存布局为 32 个通用寄存器槽位加 4 个 CSR 槽位。RV64 每槽 8 字节，共 288 字节；288 也是 16 的整数倍，不破坏原本满足要求的栈对齐。

| 内容 | 槽位 | 字节偏移 |
|---|---:|---:|
| x0 至 x31 | 0 至 31 | 0 至 248 |
| 原始 sp（x2） | 2 | 16 |
| sstatus | 32 | 256 |
| sepc | 33 | 264 |
| sbadaddr/stval | 34 | 272 |
| scause | 35 | 280 |

### 4.2 SAVE_ALL 的特殊处理

保存宏先用 `sscratch` 暂存原来的 `sp`，再把 `sp` 减去 288，建立新现场区域。其他通用寄存器按槽位存储；原来的 sp 从 sscratch 取回后写入 x2 槽位，而不是把已经减去 288 的新 sp 写进去。

`s0` 至 `s4` 原值先已保存，之后才用来搬运 CSR 值，因此不会丢失原程序的相应寄存器。本实验是内核态现场，不能把这个实现直接解释为已经完成用户栈与内核栈切换。

### 4.3 RESTORE_ALL 的恢复顺序

先取出状态和返回地址并写回 CSR：

```asm
LOAD s0, 32*REGBYTES(sp)
LOAD s1, 33*REGBYTES(sp)
csrw sstatus, s0
csrw sepc, s1
```

然后恢复 x1、x3 至 x31，最后执行：

```asm
LOAD x2, 2*REGBYTES(sp)
```

sp 必须最后恢复，因为前面的所有读取都依赖它指向当前现场。x0 恒为零，不需要恢复。scause 和 stval 是事件诊断信息，本实验返回不需要将它们写回。

CSR 恢复时借用了 s0/s1，但后续会恢复它们的原始通用寄存器值。最终执行 `sret`，而不是普通 `ret`：普通 ret 只能按返回地址寄存器跳转，不能完成特权状态返回语义。

### 4.4 汇编到 C 的调用

```asm
__alltraps:
    SAVE_ALL
    move a0, sp
    jal trap
__trapret:
    RESTORE_ALL
    sret
```

RISC-V 调用约定用 a0 传递第一个参数，所以这里传入的是当前 Trap frame 指针。`jal trap` 修改 ra，但原程序的 ra 已在现场中保存；C 函数返回到汇编后，再从现场恢复原 ra。

### 4.5 Prompt 设计与实际使用说明

这次通过连续对话向 AI 提供源码位置、运行输出和错误信息，AI 给出汇编实现，再由操作者粘贴并验证。没有证据表明实验开始前已提交一份完整独立 Prompt 文件。附带的 [trap-entry-spec.md](prompts/trap-entry-spec.md) 是根据本次接口约束**事后整理的可复用规格**，不冒充原始逐字提示词。规格强调布局对应、sp 最后恢复、a0 参数传递以及不修改既有 SAVE_ALL。

## 五、模块二：中断与异常处理

### 5.1 分派与中断编号

`trap_dispatch` 将 cause 转为有符号指针宽度整数后判断是否小于零，相当于检查最高位：最高位为 1 是中断，0 是同步异常。`interrupt_handler` 再清除最高位，取得原因编号。

本实验实际时钟中断的 cause 为 `0x8000000000000005`：最高位表示中断，低位 5 为 S 态时钟中断。

### 5.2 时钟事件设置、计数与输出

当前时钟分支为：

```c
clock_set_next_event();
ticks++;
if (ticks % TICK_NUM == 0) {
    print_ticks();
}
```

`clock_set_next_event()` 读取当前时间，并通过 `sbi_set_timer(get_cycles() + timebase)` 设置下一次事件。本次 timebase 为 100000；框架以 QEMU 10 MHz 时间基准估算约每 10 ms 一次，但 GDB 停顿和虚拟机调度会影响实际墙钟时间，不把它作为严格周期测量。

中断处理器必须重新设置下一次事件，不能只增加 ticks。`ticks` 每次增加一，`TICK_NUM=100`，每到 100 的整数倍打印一次固定字符串 `100 ticks`。多次打印并不表示 ticks 一直等于 100。

时钟中断不增加 `tf->epc`，否则可能跳过正常指令。本次 GDB 验证处理前后 epc 保持相等。

**尚未完成的差异：** 网页要求输出达到阈值后关机。当前代码没有这一计数/关机分支，压缩包也没有给出 `SHUTDOWN_AFTER` 的值。需确认采用的课程版本及阈值后补齐，并单独保留自动退出证据；现有计时测试只检查持续达到 200 ticks，不能证明关机行为。

### 5.3 非法指令和断点异常

两个异常分支分别打印 `Illegal instruction at ...` 和 `Breakpoint at ...`，再增加保存的 epc：

```c
static uintptr_t exception_instruction_length(uintptr_t epc) {
    uint16_t instruction = *(const volatile uint16_t *)epc;
    return ((instruction & 0x3) == 0x3) ? 4 : 2;
}
```

本实验限定可读的 16 位和 32 位指令编码。低两位不是 11 时按 2 字节处理，否则按 4 字节处理，避免将压缩断点和普通指令都硬编码为相同长度。评分非法指令使用 `.word 0x0000007b`，属于 4 字节测试指令。

这不是通用的所有 RISC-V 指令长度解码器：更长编码未覆盖；直接读 epc 要求该地址可读，也没有实现读指令再次异常时的完整恢复。跳过非法指令是本实验为了验证返回流程采取的策略，不等于完整 OS 遇到任意非法指令都应该忽略。

### 5.4 其他事件与测试边界

当前显式处理 S 态软件中断并输出提示；其余中断输出编号和现场；未专门处理的异常输出现场。评分中的软件中断与未知分支是直接构造 trapframe 后调用处理函数，不是实际注入硬件软件中断。

因此没有证据表明本实验完成了真实软件中断的来源清除或外部中断控制器服务。其他中断类型的完整分类仍可按课程要求进一步核对。遇到真实持续挂起的中断，只打印而不处理来源可能反复进入；未知同步异常只打印而不改变 epc，也可能重复触发。

附带的 [trap-handlers-spec.md](prompts/trap-handlers-spec.md) 记录模块二的事后规格整理，明确区分已实现行为和关机待办。

## 六、编译、运行及课程测试

### 6.1 编译与普通运行

在实验目录执行：

```bash
set -o pipefail
make 2>&1 | tee evidence/build-lab3-fixed.txt
echo "编译退出码：$?"
make qemu 2>&1 | tee evidence/run-lab3.txt
```

编译退出码为 0。普通运行完成内存自检并持续打印 100 ticks。退出采用 QEMU 的 Ctrl+A、X，不能将终止信息写成 sbi_shutdown 自动退出。

证据：[编译日志](evidence/build-lab3-fixed.txt)、[普通运行日志](evidence/run-lab3.txt)。编译日志是增量构建日志；评分日志另记录了三个测试版本的重新编译。

### 6.2 make grade 的执行机制

Makefile 的 grade 目标先清理构建目录，再把工程复制到 `/tmp/lab3_grade`，由 `tools/inject.py` 注入测试代码和调用点，再执行 `tools/grade.sh`。评分脚本分别以不同宏构建三个测试版本，启动 QEMU 并匹配结果字符串。

| 测试 | 关键验证 | 结果 |
|---|---|---:|
| GRADE_LAB3_CONTEXT | 设置寄存器测试值，触发 ebreak，返回后比较覆盖到的寄存器 | 35/35 |
| GRADE_LAB3_ILLEGAL | 非法指令后继续执行，回退处理输出符合检查 | 25/25 |
| GRADE_LAB3_TIMER | ticks 初值与持续增长，达到 200 并打印规定输出 | 40/40 |

结果见 [grade-lab3.txt](evidence/grade-lab3.txt)：`Total Score: 100/100`。测试上下文覆盖不是所有寄存器、所有状态或全部边界情况的穷尽验证；gp/tp 等在测试中有特殊保留安排。

评分脚本使用 timeout 限定运行时间，通过约定字符串判断结果，不能单凭 QEMU 退出状态判断功能通过。Makefile 在评分成功后删除临时目录，因此随后复制 `.qemu.*.out` 失败是正常清理行为，报告只引用确实保存的总体评分日志。

## 七、GDB 调试完整过程

### 7.1 启动与记录

终端 A 在实验目录执行 `make debug`。该目标通过 `-s -S` 开放端口 1234，并在客体启动前暂停。终端 B 执行 `riscv64-unknown-elf-gdb bin/kernel`，逐条输入：

```gdb
set pagination off
set logging file evidence/gdb-lab3-timer.txt
set logging overwrite on
set logging on
set architecture riscv:rv64
target remote localhost:1234
break __alltraps
continue
```

这是当次记录的命令。重新调试建议改用新日志名，避免覆盖现有证据。GDB 9.1 使用 `set logging on`。

### 7.2 入口现场

在 `__alltraps` 的 SAVE_ALL 执行前保存：

```gdb
info registers pc sp
p/x $scause
p/x $sepc
p/x $sstatus
set $entry_sp = $sp
set $entry_epc = $sepc
set $entry_status = $sstatus
```

| 项目 | 实测值 |
|---|---|
| 入口 PC | 0xffffffffc0200b8c |
| 原始 SP | 0xffffffffc0205ff0 |
| scause | 0x8000000000000005 |
| sepc | 0xffffffffc020009c |
| sstatus | 0x8000000000006120 |

此时 SIE 已因进入 Trap 被清零，SPIE 保存先前的允许状态，SPP 表示来自 S 模式。看到的 sstatus 是 Trap 入口状态，不是进入 Trap 前尚未调整的原始 CSR 值。

### 7.3 验证 C 函数收到的现场

```gdb
tbreak trap
continue
p/x tf
p sizeof(struct trapframe)
p ((unsigned long)tf == $entry_sp - sizeof(struct trapframe))
p (tf->gpr.sp == $entry_sp)
p (tf->epc == $entry_epc)
p (tf->status == $entry_status)
set $saved_tf = tf
set $ticks_before = ticks
```

实测 `tf=0xffffffffc0205ed0`，结构大小为 288；四个比较均为 1。这验证栈上布局、原 sp、epc 和 status 的保存关系。不能用进入 C 函数后的当前 sp 盲目代替 tf，因为编译器可能为 C 函数另外建立栈帧。

### 7.4 验证处理完成后的结果

```gdb
tbreak __trapret
continue
p $ticks_before
p ticks
p (ticks == $ticks_before + 1)
p ($saved_tf->epc == $entry_epc)
p ($sp == (unsigned long)$saved_tf)
disassemble __trapret
```

实测 ticks 从 0 变为 1，三个比较均为 1。处理器返回到汇编恢复入口时，sp 正确指向 Trap frame，时钟处理没有更改 epc。

### 7.5 验证恢复与 sret

本次反汇编显示最后恢复 sp 的指令在 `0xffffffffc0200c3c`，sret 位于 `0xffffffffc0200c3e`。根据当次反汇编设置断点，而不是把地址当作以后构建的固定值：

```gdb
tbreak *0xffffffffc0200c3e
continue
p ($sp == $entry_sp)
p ($sepc == $entry_epc)
p ($sstatus == $entry_status)
si
info registers pc sp
p ($pc == $entry_epc)
p ($sp == $entry_sp)
p/x $sstatus
```

sret 前三个比较均为 1。执行 sret 后回到 `kern_init` 的等待循环：PC 为 `0xffffffffc020009c`，SP 为 `0xffffffffc0205ff0`，两项比较均为 1。

sstatus 变为 `0x8000000000006002`。SIE 恢复为 1；本例中 SPIE 保持为 1、SPP 在返回时清零，返回目标特权级取自执行 sret 前保存的 SPP，不能因返回后 SPP 为 0 就说已经运行在 U 模式。

结束时禁用断点、关闭日志、detach、quit，再在终端 A 退出 QEMU。完整证据见 [gdb-lab3-timer.txt](evidence/gdb-lab3-timer.txt)。日志主要记录 GDB 输出，命令解释结合本次对话整理，并未伪造额外动态观察。

## 八、AI 协作、错误与修复

### 8.1 实际使用方式

操作者通过 Codex 桌面应用进行连续对话，逐次提供编译报错、源码片段和调试输出；AI 协助解释函数、提出实现和排查方案，并整理报告。具体模型版本未在本次用户记录中确认，提交前可据实补充。各成员独立使用情况也待确认。

上传的 `prompts/` 目录为空。本报告附带的提示词文件属于事后整理，不能替代最初逐字对话。如果课程要求原始提示词，应另行导出真实对话记录。可复用 Spec 的形成顺序为：先列接口和已有结构，再列功能、不允许改变的内容和验证方法，最后写清错误边界与未完成事项。

### 8.2 实际遇到的问题

| 问题 | 原因 | 修复与验证 |
|---|---|---|
| Ubuntu 中执行 wsl 报找不到命令 | 已在 Linux，误用 Windows 入口命令 | 直接使用当前终端，无需安装同名包 |
| sed/find 参数异常 | 多次粘贴使两条命令连在一起 | 每次只粘贴一条，确认提示符后继续 |
| 找不到 YOUR CODE 标记 | 函数已被前次编辑替换 | 查看现有定义，避免重复插入 |
| free_list、nr_free 未声明 | Lab3 尚未继承 Lab2 Best-Fit 实现 | 复制已通过测试的对应实现，再编译 |
| page_init 重定义 | 同一函数粘贴两遍，出现 `}static void...` | 备份后仅保留一份实现 |
| 两个物理地址宏未声明 | AI 最初未核对 Lab2/Lab3 常量差异 | 从 KERNBASE、KERNTOP 与偏移计算地址，保留中断保护 |
| 评分临时日志找不到 | grade 成功后删除临时目录 | 保留已有总评分日志，不虚构分项原始日志 |
| 评分满分但缺少关机逻辑 | 本地测试覆盖与网页文字要求不完全一致 | 单独列为待办，不能用测试分数替代需求核对 |

其中地址宏适配遗漏和未提前完整检查 Lab2 继承依赖属于 AI 建议中的不足。修复必须以实际源码和编译器输出为依据，而不是仅相信生成代码可以直接移植。

## 九、与操作系统原理的联系

| 知识点 | 原理含义 | 本实验体现与差异 |
|---|---|---|
| 中断与异常 | 异步事件与同步执行事件 | 时钟中断、断点和非法指令分别进入对应处理函数 |
| 上下文保存 | 保存恢复执行所需的状态 | Trap frame 保存寄存器与 CSR；不是完整进程切换 |
| 特权级控制 | 限制不同模式的操作权限 | stvec/sepc/sstatus/sret 构成 S 态 Trap 流程 |
| 调用约定 | 跨函数参数和寄存器使用约束 | a0 传指针，C 返回后恢复原现场 |
| 定时与调度 | 周期事件可驱动时间管理和抢占 | 本实验只计数与输出，没有实现任务调度 |
| 临界区 | 保护共享状态免受不合适的交错访问 | 本地关中断保护页分配器，不等于多核同步 |
| 测试覆盖 | 测试证明特定行为而非所有正确性 | 100/100 未覆盖关机、多核和所有异常 |

本实验尚未对应实现的主要 OS 内容包括用户进程的完整 Trap 切栈、系统调用分派、进程调度、完整缺页处理、外部设备中断控制器、多核并发、嵌套异常的健壮恢复以及任意非法指令处置策略。

## 十、复现与提交前检查

复现应使用源码重新编译，而不是提交 bin/obj。正常运行验证时钟，评分验证三个测试场景，GDB 日志补充实际寄存器和控制流证据。原始失败备份属于排错材料，不应当作当前实现参与构建。

提交前仍需：

1. 核对网页与所用压缩包的关机阈值约定，补齐并验证自动关机；修改后重新评分。
2. 在实际实现处补回 `lab3 function1`、`lab3 function2` 标记，并填写真实负责人的学号。
3. 三人确认分工及 AI 使用记录；若要求原始提示词，补充真实对话导出。
4. 更新本报告对应状态，最后将代码、报告、证据及提示词放入小组仓库 `labcodes/lab3`。

本文是当前已验证版本的报告，不声称上述待办已完成，也不声称已推送 GitHub。

## 十一、参考资料与证据索引

- [Lab3 练习与功能要求](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_2_1_exercise.html)，核对日期 2026-10-27。
- [Lab3 报告要求](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_4_lab_requirement.html)：Markdown、逻辑线、核心模块、Spec/Prompt、AI 错误、原理联系与未涉及知识点。
- 本次工程源码：[trapentry.S](kern/trap/trapentry.S)、[trap.c](kern/trap/trap.c)、[trap.h](kern/trap/trap.h)、[clock.c](kern/driver/clock.c)、[pmm.c](kern/mm/pmm.c)。
- 测试实现：[grade.sh](tools/grade.sh)、[test_lab3.c](tools/test_lab3.c)、[test_lab3_context.S](tools/test_lab3_context.S)、[Makefile](Makefile)。
- 实际证据：[编译](evidence/build-lab3-fixed.txt)、[运行](evidence/run-lab3.txt)、[评分](evidence/grade-lab3.txt)、[GDB](evidence/gdb-lab3-timer.txt)、[环境](evidence/versions.txt)。
