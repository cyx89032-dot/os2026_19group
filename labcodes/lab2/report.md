# 操作系统实验报告

## 一、实验基本信息与完成范围

| 项目 | 内容 |
|---|---|
| 实验名称 | Lab2：物理内存和页表 |
| 小组 | 第 19 组 |
| 小组成员 | 2412055 慈贻翔；2411861 王春晓；2414051 张玉鑫 |
| 实验记录时间 | 2026-10-13 至 2026-10-20 |
| 仓库 | https://github.com/cyx89032-dot/os2026_19group |
| 默认物理内存管理器 | best_fit_pmm_manager |
| 本报告范围 | page_init、First-Fit 分析、Best-Fit、Buddy、QEMU 页表和 TLB 调试 |

### 1.1 小组分工

**截至报告整理时，三人具体分工尚未确定。下表不沿用 Lab1 的分工，也不推定未提供的个人贡献。**

| 成员 | 本次实际承担工作 |
|---|---|
| 慈贻翔（2412055） | 模块一 |
| 王春晓（2411861） | 汇总整个实验并撰写实验报告 |
| 张玉鑫（2414051） | 模块二 |



### 1.2 验证结果与边界

| 项目 | 实际结果 | 证据 |
|---|---|---|
| page_init | 课程检查 OK | [最终评分](evidence/grade-lab2-final.txt) |
| First-Fit | 启动、自检通过 | [运行记录](evidence/run-first-fit.txt) |
| Best-Fit | 启动自检、课程检查通过 | [运行记录](evidence/run-best-fit.txt) |
| 课程评分 | Total Score: 20/20 | [评分记录](evidence/grade-lab2-final.txt) |
| Buddy System | 四组检查通过并恢复空闲页计数 | [运行记录](evidence/run-buddy.txt) |
| Sv39 页表翻译 | 读取根页表项，核对完整物理地址 | [MMU 调试日志](evidence/gdb-qemu-mmu.txt) |
| 软件 TLB | 填充后匹配；后续查询实际跳过填充路径 | [MMU 调试日志](evidence/gdb-qemu-mmu.txt) |
| M 模式启动对比 | 直接映射并填入软件 TLB，脚本检查通过 | [补全日志](evidence/gdb-qemu-bare-complete.txt) |

20/20 仅对应评分脚本覆盖的 page_init 和 Best-Fit，不代替 Buddy 或页表调试的验证。SLUB 选做扩展未实现。Lab3 不属于本次提交范围。本报告保留真实失败和修复记录，不将全部边界条件都表述为已动态验证。

## 二、环境、工程结构与总体逻辑

### 2.1 实验环境

| 工具 | 已记录版本或配置 |
|---|---|
| 系统 | Windows 下 WSL Ubuntu-24.04 |
| 交叉编译器 | riscv64-unknown-elf-gcc 10.1.0（SiFive） |
| 内核 GDB | riscv64-unknown-elf-gdb 9.1 |
| 宿主机 GDB | Ubuntu GDB 15.1，调试 QEMU 时安装 |
| QEMU | 4.1.1，virt，RV64 |
| Python | 3.12.3 |
| RAM | DTB 报告 128 MiB，物理范围 [0x80000000, 0x88000000) |

基础版本见 [versions.txt](evidence/versions.txt)。QEMU 安装版在 /usr/local/bin，源码目录下的可执行文件带 debug_info；GDB 可定位 get_physical_address，见 [调试信息检查](evidence/qemu-debug-info.txt)。因此本次复用已有带符号的二进制，没有重新编译或覆盖系统安装版。

### 2.2 本章如何逐步实现

Lab1 解决内核能否启动的问题；Lab2 进一步解决哪些物理页可用、如何组织和分配这些页，以及内核虚拟地址如何访问物理内存的问题。

启动汇编 entry.S 使用预设根页表设置 satp 并执行 sfence.vma，随后进入高虚拟地址的 C 入口。C 初始化过程读取设备树，取得内存范围，再调用物理页管理初始化。**本实验并不是由 page_init 创建完整的多级页表；启动页表由 entry.S 预置。**

物理内存管理主链为：

```text
pmm_init
  → init_pmm_manager：选择管理器，并调用 init 初始化内部状态
  → page_init：识别范围、建立 Page 数组、保留已占内存
      → init_memmap：把剩余完整物理页登记给管理器
  → check_alloc_page：调用当前管理器的 check

后续分配/释放 → alloc_pages / free_pages → 当前管理器的回调
```

三种分配器共用 pmm_manager 接口。切换算法只改变 init_pmm_manager 中的管理器赋值，不应删除 pmm_init 中对该初始化函数的调用。

| 文件 | 作用 |
|---|---|
| kern/init/entry.S | 设置启动页表和 satp、内核栈与入口 |
| kern/driver/dtb.c | 读取设备树中的内存信息 |
| kern/mm/pmm.c | 初始化物理内存、分配接口转发 |
| kern/mm/pmm.h、memlayout.h | 管理器接口、Page 描述符、地址与状态定义 |
| kern/mm/default_pmm.c | 框架 First-Fit 实现 |
| kern/mm/best_fit_pmm.c | Best-Fit 实现与原有测试 |
| kern/mm/buddy_pmm.c、buddy_pmm.h | 新增伙伴分配器 |
| kern/mm/buddy_check.c | 新增伙伴分配器测试 |

## 三、物理内存初始化 page_init

### 3.1 需求规格和实现思路

Spec 的核心是明确地址含义和保留边界：设备树返回物理基址与长度；npage 是受管理物理地址上界对应的页号，而不是空闲页数量；pages 是描述符数组的内核虚拟地址；数组从物理页号 nbase 开始建立索引。

实现顺序如下：

1. 调用 get_memory_base/get_memory_size，拒绝零范围及地址加法溢出。
2. 设置 va_pa_offset，检查本框架支持的内存布局必须包含内核装载位置。
3. 将内存上界限制在 PHYSICAL_MEMORY_END 以内并向下按页对齐，令 npage=PPN(maxpa)。
4. 通过 PADDR(end) 求内核末端物理地址，向上对齐后作为 Page 数组起点。
5. 数组长度取 npage-nbase；先检查剩余空间能否容纳描述符，再初始化各项。所有项先置为保留，引用计数与 property 清零，链表节点自连。
6. 将描述符数组末端向上对齐得到 free_begin。只把 [free_begin,maxpa) 中的完整页面交给 init_memmap。

如果没有剩余完整页，不调用 init_memmap(base,0)。无效内存信息和描述符空间不足等情况通过 panic 终止，避免越界写。

### 3.2 地址与数量核对

在默认 RAM 下：

```text
DRAM_BASE = 0x80000000
maxpa     = 0x88000000
nbase     = 0x80000
npage     = 0x88000
描述符数量 = npage - nbase = 32768
页大小     = 4096 字节
```

RV64 下当前 Page 结构占 40 字节，描述符总占用为 32768×40=0x140000 字节。Best-Fit 运行中，内核 end=0xffffffffc0206078，转成物理地址后向上对齐得到描述符起点 0x80207000；加上 0x140000 得到 free_begin=0x80347000。

```text
空闲页数 = (0x88000000 - 0x80347000) / 4096 = 31929
```

固件、内核、描述符数组以及对齐产生的间隙都不进入空闲链表。此实现适用于课程固定装载地址和映射范围，不是通用的多内存段热插拔管理器。

### 3.3 Spec、Prompt 与验证


课程 check page init 为 OK。日志证明默认布局运行正确；不能仅凭这项结果声称所有非法 DTB 和所有极限内存大小均已运行覆盖。

## 四、First-Fit 连续物理页分配分析

### 4.1 核心函数

default_init 建立空闲链表头并清空空闲页计数。default_init_memmap 把一段可用页设置为可管理状态，以块首页的 property 记录连续页数，并登记空闲块。

default_alloc_pages 从空闲链表开始查找第一个 property>=n 的块。若刚好满足则整块取出；若大于需求，则分配前 n 页，剩余部分成为新的空闲块。它维护块头状态及 nr_free，找不到足够大的连续块时返回 NULL。

default_free_pages 清理待释放页状态，将释放块按地址放回链表，检查前后相邻块是否物理连续，能够合并则扩大块并删除冗余块头。合并不额外增加空闲页数，页数只在释放时增加一次。

### 4.2 是否有改进空间

有。链表线性扫描在空闲块较多时开销较大，反复从头搜索还可能形成前部小碎片。可考虑 Next-Fit 的搜索游标、按大小分级索引，或维护支持大小检索的平衡树；需要同时保留地址邻接信息以便合并。这些是改进建议，本次未实现，不声称经过性能比较。

First-Fit 的优点是策略简单且找到足够大的块即可结束；缺点是结果依赖块顺序，外部碎片可能使总空闲页数足够但连续分配失败。自检通过说明功能符合所执行的检查，不证明它比 Best-Fit 更快或更省内存。

## 五、Best-Fit 实现

### 5.1 Spec 与数据结构

沿用 free_area_t、free_list 和 nr_free 的状态约定，保证原有测试可以使用这些状态。空闲链表按地址递增排列，块头 PageProperty 为真，property 表示该块页数。

Spec 要求：选择满足 n 页需求的最小块；同样大小取地址较低者；拆分后尾部仍处于正确的地址顺序；释放时合并前后相邻空闲块；保留原有 basic_check 和 best_fit_check。

### 5.2 分配、释放与复杂度

best_fit_alloc_pages 扫描链表，只有候选块比当前最佳块更小时才更新 best。因此同样大小保留先遇到、地址较低的块。找到正好 n 页的块后立即结束。取出块时保存原链表位置，剩余尾块插回该位置，并更新标志与计数。

best_fit_insert_merge 先按地址插入，再尝试合并前驱，之后以合并后的块继续检查后继。合并时清除被吸收块的块头标志并删除相应链表节点。init_memmap 和 free_pages 复用该函数。

设空闲块数为 F，申请搜索最坏 O(F)；释放查找插入位置 O(F)，逐页状态初始化另需 O(n)。仅空闲页总数足够并不保证存在合适的连续块。

### 5.3 改进空间与验证

可使用按大小有序的索引降低最小合适块搜索成本，同时使用地址索引支持合并。但更复杂的数据结构增加维护成本；Best-Fit 也可能留下很多难以利用的小碎片，不能断言它总比 First-Fit 更好。

实际测试为 check best fit: OK，完整评分 20/20。报告整理时将最终文件与原始 lab2.zip 比较，两个框架测试在忽略注释、空白后保持一致；First-Fit 文件保持原样。完整需求和提示词见 [best_fit.md](prompts/best_fit.md)。

## 六、Buddy System 的实现与独立测试

### 6.1 实现规格

Buddy 使用 0 至 15 阶空闲链表，order 阶块含 2^order 页。物理页号必须按块页数对齐。初始化把任意起止的连续可用区间拆成合适的对齐块，避免简单向下取整丢弃尾部内存。

申请 n 页向上取整到 2 的幂。例如申请 3 页实际占用 4 页。先找到足够大的阶，再逐级拆分，把另一半插回对应链表。nr_free_pages 按实际占用的页数扣减。

释放接口要求传原始申请值，例如 free_pages(base,3) 内部归还整个 4 页块。通过以下公式寻找伙伴：

```text
伙伴物理页号 = 当前块物理页号 XOR (1 << order)
```

只有伙伴位于描述符范围内、处于空闲状态且阶数相同，才递归合并。相邻块不一定互为伙伴，不能仅凭连续地址合并。

### 6.2 状态约定与限制

空闲块头的 property 存阶数；分配块头的 property 存原始申请页数。新增 flags 位 2 表示已分配，位 3 表示分配块头。释放前检查块头、页数、对齐、各页分配状态与引用计数，再清理并合并。后续实验若新增 flags 用途，应检查冲突。

零页申请和非法释放作为调用错误由断言处理；合法但无法满足的申请返回 NULL。当前模块并未实现并发锁。虽阶数查找、拆分、合并的层数有限，逐页状态维护仍包含 O(实际块页数) 开销，不能把整个实现无条件写成 O(log n)。

### 6.3 测试设计与为何有效

测试单独放在 buddy_check.c，通过管理器公开回调运行，不依赖私有空闲链表。

| 测试 | 检验目的 |
|---|---|
| 申请 1、3、5、8、17 页 | 验证不同阶数、非 2 的幂取整、对齐与实际扣减 |
| 分配范围两两比较 | 检验不同申请不重叠，且没有保留页被分配 |
| 乱序释放并逐次核对计数 | 检验回收计数，避免简单正序掩盖错误 |
| 超过最大阶数的申请 | 验证失败返回 NULL，计数不变 |
| 单页申请直到耗尽 | 位图检查没有重复分配，最终分配数等于初始空闲页数 |
| 耗尽后申请 1、2 页 | 验证资源不足路径 |
| 只释放一个对齐 8 页范围，再申请 8 页 | 其他页均被占用，只有三级合并正确才能返回该范围 |
| 将上述 8 页块再次拆成单页 | 验证拆分及单页不重复 |
| 最终释放全部测试页 | 检验页数恢复，排除本轮可观测泄漏 |

运行日志显示：

```text
buddy check: rounding, alignment, non-overlap and accounting OK
buddy check: full exhaustion and allocation failure OK
buddy check: multi-level merge and split OK
buddy check: all pages restored OK
check_alloc_page() succeeded!
```

测试依赖课程默认内存规模：描述符不超过 32768 个，至少 128 可用页，并存在对齐的连续 8 页可用范围。32 KiB 静态位图占用内核 BSS，所以 Buddy 测试时空闲页为 31921，而 Best-Fit 为 31929。链接布局也会随代码变化，不能把旧空闲页数硬编码为所有构建都必须满足的结果。

非法释放会终止内核，本轮正常测试没有刻意触发这些断言，不能声称已执行所有负面测试。实际 QEMU 验证见 [run-buddy.txt](evidence/run-buddy.txt)。

### 6.4 两份提示词及来源

实现规格与测试规格分别保存于 [buddy-implementation.md](prompts/buddy-implementation.md)、[buddy-tests.md](prompts/buddy-tests.md)。

## 七、QEMU 页表与 TLB 调试

### 7.1 调试对象和方法

普通宿主机 GDB 调试 x86-64 QEMU 进程；RISC-V GDB 才用于连接客体内核。本次分支任务采用普通 GDB 直接启动带符号 QEMU，并对真实被访问地址设置条件断点。未同时启动第二个内核 GDB，不声称完成了双 GDB 联调；这种方式仍能观察 QEMU 内的取指翻译链。

QEMU 路径：/opt/riscv/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64。核心观察函数为 get_page_addr_code、tlb_fill、riscv_cpu_tlb_fill、get_physical_address 和 tlb_set_page_with_attrs。

### 7.2 Sv39 地址翻译的实际数值

| 观测项 | 实测值 |
|---|---|
| 请求地址 | 0xffffffffc02000d8 |
| 请求类型 | MMU_INST_FETCH，取指 |
| mmu_idx | 1，本次 S 模式上下文 |
| satp | 0x8000000000080205 |
| MODE | 8，即 Sv39 |
| 根页表物理地址 | 0x80205000 |
| levels / ptidxbits / ptesize | 3 / 9 / 8 |
| 根级索引 idx | 0x1ff，即 511 |
| 页表项物理地址 | 0x80205000 + 511×8 = 0x80205ff8 |
| 页表项 pte | 0x200000cf |

satp 的低 44 位页号左移 12 位得到根页表物理地址。Sv39 的索引分别来自虚拟地址的 VPN[2:0]，每级 9 位；循环从最高级开始。代码在 cpu_helper.c 第 252 行用 ldq_phys 读取页表项，第 256 行检查 V，第 259 行判断 R/W/X 是否全零。全零才继续下一层；本次并不是这种情况。

pte 的低字节 0xcf 表示 V、R、W、X、A、D 为 1，U、G 为 0。根级已经是叶子项，所以映射 1 GiB 大页，只读取一级，不需要走满三级。entry.S 最后一个根页表项正好是 (0x80000<<10)|0xcf，与观测一致。A/D 已预置，不能声称本次观察到了首次访问引起的 A/D 写回。

### 7.3 完整物理地址与函数输出的区别

```text
叶子物理基址 = (pte >> 10) << 12 = 0x80000000
1 GiB 页内偏移 = VA & 0x3fffffff = 0x2000d8
完整物理地址 = 0x802000d8
```

但 QEMU 4.1.1 的 get_physical_address 在该分页路径第 334 行构造用于软件 TLB 的 4 KiB 页基址：它拼入虚拟页号的相应低位再左移 12 位，返回 0x80200000，未包含低 12 位的 0xd8。因此正确验证是：

```text
函数输出页基址 + (VA & 0xfff) = 0x80200000 + 0xd8 = 0x802000d8
```

调试中直接比较完整地址与函数输出得到 0，补回偏移后比较得到 1。这是本次通过实际源码纠正理解的重要记录，不是翻译失败。

### 7.4 关键调用链与分支

```text
tb_htable_lookup
 → get_page_addr_code
    → 检查 entry->addr_code 是否匹配当前地址
    → 主 TLB 未命中时检查 victim TLB
    → 仍未命中则 tlb_fill
       → riscv_cpu_tlb_fill
          → get_physical_address：权限/模式判断、页表读取、叶子检查
          → 检查成功后 tlb_set_page
             → tlb_set_page_with_attrs：构造并复制 TLB 表项
    → 用 addr + entry->addend 访问宿主机内存
```

get_physical_address 还检查规范地址、PTE 有效性、保留权限组合、U/S 权限、大页物理对齐、访问类型与 R/W/X 权限等。普通模式下的地址直接映射和 Sv39 页表遍历是不同分支。QEMU 代码支持失败异常路径，本次没有人为构造每一种页故障。

### 7.5 TLB 填充与实际命中

填充函数实参为 vaddr=0xffffffffc0200000、paddr=0x80200000、prot=7、mmu_idx=1、size=4096。prot=7 是 QEMU 内部读写执行权限组合，不是原始 PTE 的 0xcf。

在 cputlb.c 中，第 804 行计算 tn.addend，第 812 行设置取指标记，第 835 行复制到实际表项。优化构建下部分局部变量不可见，因此改在 get_page_addr_code 第 1036 行检查填充后的 entry。

实际表项：

```text
addr_read  = 0xffffffffc0200000
addr_write = 0xffffffffc0200400
addr_code  = 0xffffffffc0200000
addend     = 0x80001fe00000
```

addr_write 带额外低位标记，不能把它当成另一个虚拟页基址；源码对写入还可能设置 NOTDIRTY、MMIO 等软件标志。对本次普通 RAM 取指，addr_code 与按页对齐后的请求地址相等，比较结果为 1。

再次命中 cputlb.c 第 1030 行的查询断点时，addr_code 仍匹配。执行 next 后直接到第 1039 行，跳过第 1031—1036 行的未命中路径。这是后续访问实际命中的证据，而不仅是“刚填充完便匹配”的检查。

同次运行中：

```text
(uint64_t)VA + addend = 0x7fffe00000d8
```

这是宿主机虚拟地址，不是客体物理地址。加法按 64 位地址运算。get_page_addr_code 后续返回的是 ram_addr_t，不应将其直接解释成客体物理地址。宿主地址依赖当前进程映射，重新运行可能改变。

### 7.6 未开启分页的启动对比

重新启动后观察到 addr=0x1000、satp=0、mmu_idx=3。M 模式取指在 get_physical_address 的直接映射分支返回 physical=0x1000；调用栈仍经过 get_page_addr_code→tlb_fill→riscv_cpu_tlb_fill。

最后通过自动脚本再次验证：TLB 填充参数 address=pa=0x1000，mmu_idx=3，填充后 entry->addr_code=0x1000，比较结果为 1，输出 BARE_TLB_CHECK_PASSED。日志文件名使用 bare，但**本次实际验证的是 M 模式启动直接映射，不能写成 S 模式 satp.MODE=Bare 分支的独立测试**。

由此可见，QEMU 软件 TLB 不只缓存客体页表遍历结果，还承载模拟器访问宿主内存及内存区域属性所需的信息。真实 CPU 不需要 QEMU 的宿主指针 addend；未启用地址翻译也不需要 Sv39 页表遍历。具体硬件是否还有其他缓存属于实现细节，不能把“没有页表查询”泛化为“硬件没有任何缓存”。

本次没有测量硬件 TLB 容量、替换策略或性能，也未动态覆盖 victim TLB 命中及全部异常分支。

## 八、真实问题、AI 协作与修复

| 问题 | 原因与处理 |
|---|---|
| 将 pmm_manager 赋值输入 shell | C 语句应写入源码；shell 将其当命令并报错，改在编辑器修改 |
| 编译通过但停在 physical memory 输出 | 错把 pmm_init 内初始化调用替换成管理器赋值；恢复调用，并在 init_pmm_manager 内选 Best-Fit，随后自检通过 |
| GDB 多行粘贴报错 | 多行被拼为一条命令；改为逐条输入，最终补充任务用命令文件自动执行 |
| 将页基址与完整物理地址直接比较 | AI 最初给出的预期不准确；阅读第 334 行后补回低 12 位偏移，比较通过 |
| te、tn 不可见，后续指针读取连续失败 | 当前优化调试上下文未提供变量；不再使用保存失败的变量，改在调用者读取可见的 entry |
| 重复设置同位置断点 | 出现断点 6、7；不会推翻已观察的命中结果，后续禁用重复断点 |
| continue 后 QEMU 已结束，局部变量不可读取 | 程序退出后没有对应栈帧；重新启动并设置断点，最终用脚本补全证据 |
| 脚本结束显示 killed | 脚本检查通过后主动结束其启动的 QEMU，是清理行为，不是内核 panic |

AI 的作用包括整理规格、生成实现和测试、解释源码、设计调试命令及整理报告；人工完成终端执行、代码接入和结果反馈。AI 的解释并非天然正确，本次物理页基址误判就是必须以源码和运行事实复核的例子。

使用工具为 Codex；具体模型版本没有可靠记录，报告不猜测。其他成员是否使用 AI、使用何种工具及实际分工，需由成员据实确认。提示词附录是整理记录，不伪称完整逐字聊天历史；调试交互中的实际输出保留在 evidence。

## 九、实验知识点与 OS 原理的关系

| 知识点 | 本实验体现 | 与一般 OS 原理的差异或边界 |
|---|---|---|
| 物理内存发现 | DTB 提供范围 | 真实平台还可能通过固件内存图发现多个 RAM/保留区域 |
| 页描述符与保留 | Page 数组、Reserved、引用计数 | 当前为教学实现，未包含完整 NUMA、热插拔、回收元数据 |
| 连续分配与碎片 | First-Fit、Best-Fit、Buddy | Best-Fit 并非全局最优；Buddy 取整产生内部碎片 |
| 虚拟地址和物理地址 | 固定偏移、高地址内核映射 | 不是每个用户地址空间都具有相同的简单线性关系 |
| 多级页表与大页 | Sv39 根级 1 GiB 叶子项 | 三级结构不要求每次走满三级；映射范围也不代表全部背后都有 RAM |
| 地址翻译缓存 | 软件 TLB 查询、填充、命中 | QEMU 缓存带宿主内存信息，不是硬件 TLB 的逐周期模型 |
| 权限检查 | PTE V/R/W/X/U 与访问类型 | 本实验未构建用户进程隔离和完整页故障恢复 |

OS 原理中重要但本实验未完整实现的内容包括：进程调度、上下文切换、多进程地址空间、请求分页、换页与页面置换、写时复制、文件页缓存、SMP 并发同步、TLB shootdown、完整文件系统和设备中断处理。不能因为能分配页、能建立启动映射，就称为完整虚拟内存子系统。

## 十、选做思考与未实现扩展

如果 OS 事先不知道可用物理内存，应首先查询平台提供的可信描述：设备树、BIOS/UEFI 内存图或相应固件接口。不能把所有可寻址空间都当 RAM，也不宜随意写入未知地址探测，否则可能触碰 MMIO、固件或保留区域。需对每个区域分类、检查边界并保留固件/内核占用。此处为原理讨论，没有实现额外探测算法。

SLUB 的对象级分配扩展未实现，也未生成或运行相应测试；不申报该扩展的完成情况。

## 十一、复现与提交说明

默认保持 Best-Fit。在实验目录执行 make，再执行 make qemu，按 Ctrl+A 后按 X 退出；make grade 验证课程两项评分。日志均已保存，不应为了整理报告覆盖原始失败日志。

演示 Buddy 时，仅将 init_pmm_manager 中的管理器赋值改为 &buddy_pmm_manager，保留初始化调用，重新 make 和 make qemu。观察四组检查后恢复 &best_fit_pmm_manager。Buddy 不由原课程 20 分脚本直接计分，需引用其独立运行日志。

QEMU 源码调试用普通 gdb 打开带符号的 QEMU，使用条件断点和日志记录。具体地址、源码行号与本次 4.1.1 构建有关，其他版本需重新核对。补全脚本为 [bare-final-check.gdb](evidence/bare-final-check.gdb)，它启动检查并在成功后主动结束模拟器。

本目录应提交到仓库 labcodes/lab2。保留代码、报告、prompts 与 evidence；bin、obj 是可重建产物，不作为源码提交。evidence 下的 before/best-fit-passed/buddy-passed 是历史备份，可能含原始占位标记；判断当前实现是否完成应检查 kern 下的当前源码。

提交前待确认：三人实际分工与 AI 使用信息。当前两处必做 lab2 标记已保留且填写 2411861；若实际责任归属不同，应据实调整，不能凭报告整理者推定。

## 十二、资料来源

- [Lab2 练习与任务要求](http://oslab.mobisys.top/lab2026/_book/lab2/lab2_2_1_exercise.html)
- [QEMU 页表查询调试要求](http://oslab.mobisys.top/lab2026/_book/lab2/lab2_4_qemu_mmu_debug.html)
- [实验报告要求](http://oslab.mobisys.top/lab2026/_book/lab2/lab2_5_labs_requirement.html)
- 本次提供的 lab2.zip、最终源码、运行与 GDB 日志。QEMU 函数分析以日志中实际显示的 4.1.1 源码为依据。

## 附录：Spec 与提示词记录全文

以下为本次保存的需求与提示词记录。早期文件的验证状态已在报告整理时依据日志补录；Buddy 文件保留 AI 协助起草的来源说明。

### page_init.md

```text
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
```

### best_fit.md

```text
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
```

### buddy-implementation.md

```text
# Buddy 实现提示词草案（AI 协助整理）

这是本次辅助开发中由 AI 整理的规格草案，并非此前由学生独立提交的提示词原文。请阅读、按自己的设计修改，再用于后续迭代；实验报告如实记录实际交互和贡献。

请参考本工程 pmm.h、memlayout.h、list.h 和现有分配器，从零实现 Buddy System。保持 struct Page 和 pmm_manager 接口不变，新建 buddy_pmm.c/h，保留 First-Fit、Best-Fit 及其原测试。

使用 0 至 15 阶空闲链表；每块包含 2^order 页，按物理页号对齐。初始化要完整管理任意非对齐的连续可用区间，不丢弃尾部页面；不得把保留页加入空闲区。伙伴通过物理页号异或块大小计算，合并前验证上下界、空闲状态和阶数。

申请 n>0 页向上取整到 2 的幂，拆分大块；不足或超过支持的最大块时返回 NULL。free_pages(base,n) 必须传原始申请页数，内部归还完整块并递归合并真正的伙伴。记录分配状态和原始请求，以断言拒绝重复释放、错误页数及内部页释放。nr_free_pages 统计实际可分配页数，包含取整成本。零页申请和非法释放为调用错误，用断言处理。

分别说明内部碎片、空闲链表复杂度、PG flags 新增位的用途及移植限制。仅在 init_pmm_manager 内切换管理器，保留 pmm_init 中的 init_pmm_manager() 调用。
```

### buddy-tests.md

```text
# Buddy 测试提示词草案（AI 协助整理）

本文件为本次开发时 AI 整理的测试规格，不应写成学生此前独立设计的交互记录。请审阅并结合自己的理解修改，保存真实后续交互。

根据实现接口另写 buddy_check.c，通过公开 manager 回调验证 Buddy，不读取其私有链表，也不修改已有 First-Fit/Best-Fit 测试。

测试 1、3、5、8、17 页申请，验证返回地址对齐、分配区间不重叠、无保留页和向上取整后的空闲页计数。乱序释放后核对页数恢复。超大申请应返回 NULL，计数不变。

逐页耗尽真实空闲区，用描述符索引位图检查没有重复页、没有越界、计数一致，耗尽后申请 1 或 2 页失败。选择已分配页中物理对齐的连续 8 页，交错释放后申请 8 页，必须返回该范围，以验证三级伙伴合并；再拆成 8 次单页申请检查拆分。最终释放全部测试页，确保初始空闲页数恢复。

说明测试前提：本课程默认 128MiB RAM，描述符不超过 32768 个，可用页至少 128 且存在对齐的 8 页范围。32KiB 静态测试位图增加内核 BSS，减少可用页属正常现象。内核正常测试不故意触发致命断言；非法释放应另用隔离测试进程检查，不能假称本测试已覆盖所有错误路径。
```
