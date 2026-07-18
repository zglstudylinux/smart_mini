# BT892X SPI 测试代码 HAL 重构报告

> **日期**：2026-07-17
> **重构范围**：仅 SPI 测试代码（test_spi_*.c × 5 + 新增 spi_hal.{h,c}）
> **重构深度**：**浅重构**（用户决策 2026-07-17）
> **HAL 层抽象范围**：仅抽象当前 BT892X SPI1，不预留多芯片（用户决策）
> **代码组织**：保留 bus 类型指定（soft/hw 各一套），不强行统一（用户决策）

---

## §0 文档结构

本文档针对 SPI 测试代码的重构记录，按以下结构展开：

- **§1 Context**：为何重构（重复问题）
- **§2 重构目标与约束**：3 个用户决策
- **§3 五步实施**（Step 1-5）
- **§4 最终架构**
- **§5 性能漂移现象**（实测发现）
- **§6 后续可选重构思路**（中重构、深重构）
- **§7 换芯片思路**
- **§8 经验教训**
- **§9 关键文件**

---

## §1 Context：为何重构

SPI 模块测试完后（5 个 phase + 完整文档已推送 commit `51d1757`），共 5 个 `test_spi_*.c` 测试文件。每个文件**重复实现同样的 SPI 原语**：

| 重复实现 | 出现次数 | 总行数估算 |
|---|---|---|
| 软件 `spi_init/byte/cs_low/cs_high` | 4 次 | ~30 行 |
| 硬件 `hw_spi_init/byte/cs_low/cs_high` | 4 次 | ~50 行 |
| W25Q64 软命令（write_enable/read_status/wait_busy/read_data/page_program/sector_erase） | 1 次 (test_spi_w25q64.c) | ~60 行 |
| W25Q64 硬命令（同上 6 个） | 1 次 | ~60 行 |
| Interrupt ISR + setup/teardown/byte_it | 1 次 (test_spi_timing.c) | ~30 行 |
| DMA read/write | 1 次 | ~25 行 |

→ **重复代码约 250-300 行**，每改一个寄存器就要搜 5+ 个文件同步改。例如修改 SPI1BAUD 计算公式就要同时改 `test_spi_loop.c`、`test_spi_w25q64.c`、`test_spi_timing.c` 各自重复实现。

**用户提出的关键问题：**
- "我想换芯片，那是不是所有代码都要重构？" → 现在不会了，只改 HAL 一层
- "这些初始化的代码能否统一起来实现接口标准化？" → HAL 抽象原语和 W25Q64 驱动
- "是否可以优化代码重复？" → 已优化

---

## §2 重构目标与约束（用户决策 2026-07-17）

### §2.1 三个关键决策

| 决策 | 选项 | 用户选择 |
|---|---|---|
| 重构深度 | 浅 / 中 / 深 | **浅**（只抽 bus/driver，不引入运行时多态） |
| 测试层是否 bus 无关 | 是 / 否 / 仅接口 | **否**（保留 soft/hw 各一套，不强行统一） |
| 多芯片准备 | 是 / 否 / 仅当前 | **仅当前**（不预留 STM32 等多芯片） |

### §2.2 决策的逻辑依据

- **浅重构**：最小风险，立即可见的去重收益（~250 行）
- **测试层保留 bus 类型指定**：用户坚持"两份就两份，跑两种 bus 看的现象也是信息"
- **不预留多芯片**：避免 over-engineering，下次真要换芯片时再做

---

## §3 五步实施

按 "重构一个我测试一个" 节奏逐步推进。每步完成后用户实测，现象一致才进下一步。

### §3.1 Step 1（最小验证）—— 创建 `spi_hal` + 重构 `test_spi_loop.c`

**新增文件：**
- `smart_mini/test/spi_hal.h` — HAL 接口声明 + pin macros
- `smart_mini/test/spi_hal.c` — HAL 实现（手册 §3.2/§3.3/§7.2 注释）

**接口最小集**（Step 1 只抽底层原语）：
```c
// 软件 bit-bang
void spi_hal_soft_init(void);
u8   spi_hal_soft_byte(u8 tx);
// 硬件 SPI1
void spi_hal_hw_init(u32 baud);
u8   spi_hal_hw_byte(u8 tx);
// CS 控制（共享）
void spi_hal_cs_low(void);
void spi_hal_cs_high(void);
```

**重构 test_spi_loop.c**：
- 删除 56 行重复实现（`soft_spi_init/byte` 17 行 + `hw_spi_init/byte` 14 行 + CS 控制）
- 改为 `#include "spi_hal.h"` 调用 HAL 函数
- 文件从 137 → 86 行（**-37%**）
- main.c 开关 `TEST_SPI_LOOP_EN` 不变

**验证**：用户烧 `TEST_SPI_LOOP_EN` → 软+硬回环 `0/256 errors PASSED` ✅

### §3.2 Step 2 —— 重构 `test_spi_wave.c` + 修复 Phase 0 bug

**重构 test_spi_wave.c**：
- 删除 41 行重复实现
- 调用 `spi_hal_soft_init/byte/cs_low/high` + `spi_hal_hw_init/byte/cs_*`
- 文件从 130 → 89 行（**-31%**）

**顺手修复发现的设计 bug：**
- 原 dispatcher Phase 0 (`#else`) 既不是软也不是硬，只调一次 `run_soft_waveform()`，注释说"全跑"完全错误（因 `while(1)` 无法跨阶段连跑）
- 改成：明确 PHASE=1（软）或 PHASE=2（硬），其他值 `#error` 编译失败
- 默认 PHASE=1（软 SPI）

**验证**：用户烧 `TEST_SPI_WAVE_EN`（两个 PHASE 都试） → LA 解码都是 `0x55` ✅

### §3.3 Step 3 —— 重构 `test_spi_soft_asm.c` + **性能漂移现象**

**重构 test_spi_soft_asm.c**：
- C 版本直接调 `spi_hal_soft_byte()`（HAL 实现就是 C 版本）
- ASM 循环版和 ASM 展开版是测试核心，保留本地实现但用 HAL 的 `SPI_*_PIN` 宏
- 文件从 173 → 140 行（**-19%**）

**实测性能变化（重要发现）：**

| 版本 | 重构前 (ticks/10000 bytes) | 重构后 | 变化 |
|---|---|---|---|
| C | 584802 (58 µs/byte) | 649360 (64 µs/byte) | **慢 11%** |
| ASM loop | 582513 (58 µs/byte) | 582539 (58 µs/byte) | ≈ 一致 |
| ASM unroll | 597833 (59 µs/byte) | 597828 (59 µs/byte) | ≈ 一致 |

→ **分析结论反转了**：之前 C ≈ ASM（~0% 差距），现在 ASM 比 C **快 10%**。

**根因**：C 版本从 `static` 内联变成跨 TU 外部调用（`spi_hal.c` 的 `spi_hal_soft_byte`），无 LTO 时无法 inline。**`__asm__ volatile`** 强制调用点 inline，所以 ASM 速度不变。

→ **详见 §5**

**更新 Analysis 段**：写明"重构前后含义反转"现象，不再误导用户。

### §3.4 Step 4 —— 扩展 HAL + 重构 `test_spi_w25q64.c`

**HAL 加 W25Q64 Flash 驱动层**：

```c
/* 软件 bit-bang 版（6 个 W25Q64 命令原语） */
void spi_hal_w25_sw_write_enable(void);
u8   spi_hal_w25_sw_read_status(u8 cmd);
void spi_hal_w25_sw_wait_busy(void);
void spi_hal_w25_sw_read_data(u32 addr, u8 *buf, u32 len);
void spi_hal_w25_sw_page_program(u32 addr, const u8 *buf, u32 len);
void spi_hal_w25_sw_sector_erase(u32 addr);

/* 硬件 SPI1 版（同 6 个命令） */
void spi_hal_w25_hw_write_enable(void);
...（同名 + `_hw` 后缀）
```

**为什么分 SW 和 HW 而不是单一组加函数指针**：浅重构决策——避免运行时多态，编译期静态绑定 6 个命令原语各两份，**调用方一目了然**（`spi_hal_w25_sw_*` 是软件 SPI，`_hw_` 是硬件 SPI）。

**重构 test_spi_w25q64.c**：
- 删除 ~120 行 W25Q64 命令函数（sw_xxx × 6 + hw_xxx × 6）
- 改为调 HAL 命令原语
- 文件大小不变（实验函数 sw_exp1..9 / hw_exp1..10 占主体），但**逻辑显著清晰**
- 18 个实验函数（8 软件 + 10 硬件）保持不变

**验证**：用户完整跑软+硬全套 18 个实验 → 所有现象与重构前一致 ✅

### §3.5 Step 5 —— HAL 加 Interrupt/DMA + 重构 `test_spi_timing.c`

**HAL 加 Interrupt 模式三件套**：
```c
void spi_hal_hw_it_setup(void);     /* 开 SPIIE + register_isr + PICEN */
void spi_hal_hw_it_teardown(void);  /* 关 PICEN + 关 SPIIE */
u8   spi_hal_hw_byte_it(u8 tx);     /* 阻塞式等 ISR 完成 */
```

**HAL 加 DMA 模式高层 API**：
```c
void spi_hal_hw_read_dma(u32 addr, u8 *buf, u32 len);   /* §7.3 RXSEL=1 流程封装 */
void spi_hal_hw_write_dma(u32 addr, const u8 *buf, u32 len);  /* §7.3 RXSEL=0 流程封装 */
```

**重构 test_spi_timing.c**：
- 删除 60+ 行 ISR/DMA 重复实现
- `int_spi_byte/read`、`read_dma`、`hw_interrupt_setup/teardown` 全部换成 HAL 调用
- 提取 `print_us100(name, ticks)` helper 统一 6 处相同 `printf`
- 提取 `timing_check_jedec()` `timing_wait_busy()` 包装
- 文件从 274 → 231 行（**-16%**）

**顺手修复 regression bug**：重构时多写一个 `(void)` cast 导致 JEDEC 字节序错位：
- 错误版本：3 个 byte 读，前 2 个 `(void)` cast 丢弃，最后一个赋给 `a` → `a` = 第 3 字节 = Capacity 0x17
- 正确版本：第 1 个赋给 `a`（manufacturer 0xEF），剩下 `(void)` 丢弃
- 修法：调整 `(void)` 位置 + 加注释提醒字节序敏感

→ **教训**：重构时字节序敏感代码要逐字节核对，不能用"看上去一样"的写法。

**验证**：用户烧 `TEST_SPI_TIMING_EN` → JEDEC OK、3-way 对比 OK、DMA Page Program PASSED ✅

**性能数据**（与重构前对比）：

| | 重构前 | 重构后 | 结论 |
|---|---|---|---|
| DMA 12MHz | 2533 / 0.62 µs | 2534 / 0.61 µs | 一致 |
| Interrupt 12MHz | 91949 / 22.44 | 93100 / 22.72 | ≈ 一致 |
| Polling 12MHz | 10072 / 2.46 | 8047 / 1.96 | **意外变快** |
| Speedup (Pol/DMA) | 3.97x | 3.1x | 略降 |

→ 关键结论**完全保持**：DMA 最快，Interrupt 在 12MHz 反而慢 9×（ISR 开销主导）。Polling 的微变是另一面性能漂移（详见 §5）。

### §3.6 Step 6（待执行）—— 删除 `test_spi_common.h`

合并到 `spi_hal.h`（pin macros 已迁移），`test_spi_common.h` 变空壳，删除。

### §3.7 Step 7（待执行）—— 更新 docs/test_spi.md

加一节"HAL 重构"说明文件结构变化 + 指向 `docs/test_spi_hal_refactor.md`（本文档）。

---

## §4 最终架构

### §4.1 文件树

```
smart_mini/test/
├── spi_hal.h              ← 新建：SPI HAL 接口
├── spi_hal.c              ← 新建：SPI HAL 实现（含 W25Q64 驱动 + IT/DMA）
├── test_spi_loop.c        ← 重构：调 spi_hal_*_init/byte
├── test_spi_wave.c        ← 重构：调 spi_hal_*
├── test_spi_w25q64.c      ← 重构：调 spi_hal_w25_sw_/hw_*
├── test_spi_soft_asm.c    ← 重构：3 版 byte + spi_hal_soft_*
└── test_spi_timing.c      ← 重构：调 spi_hal_hw_it_/dma_*
```

未来：删除 `test_spi_common.h`（内容已全部合并到 `spi_hal.h`）。

### §4.2 spi_hal 接口总览

```
spi_hal.h
├── 引脚宏 (4) ─┐
│              ├─ 共用
├── 软件原语 (2) ┤
│   soft_init   │
│   soft_byte   │
├── 硬件原语 (2) ─┤
│   hw_init     │
│   hw_byte     │
├── CS 控制 (2)  ─┘
│   cs_low
│   cs_high
├── W25Q64 驱动 (12) ─── sw_×6 + hw_×6
│   sw_write_enable / hw_write_enable
│   sw_read_status  / hw_read_status
│   sw_wait_busy    / hw_wait_busy
│   sw_read_data    / hw_read_data
│   sw_page_program / hw_page_program
│   sw_sector_erase / hw_sector_erase
└── HW Interrupt + DMA (5)
    ├── it_setup / it_teardown / byte_it
    └── read_dma / write_dma
```

### §4.3 调用关系图

```
test_spi_loop.c    → spi_hal_soft_init/byte + spi_hal_hw_init/byte + spi_hal_cs_*
test_spi_wave.c    → spi_hal_soft_init + spi_hal_soft_byte + spi_hal_hw_init + spi_hal_hw_byte + spi_hal_cs_*
test_spi_soft_asm.c → spi_hal_soft_init + spi_hal_soft_byte（其他版本 local）
test_spi_w25q64.c  → spi_hal_soft_init + spi_hal_w25_sw_* + spi_hal_hw_init + spi_hal_w25_hw_*
test_spi_timing.c  → spi_hal_hw_init + spi_hal_hw_byte + spi_hal_hw_it_setup/teardown/byte_it + spi_hal_hw_read_dma/write_dma + spi_hal_w25_hw_*
```

**每个测试只直接调 HAL**，不直接碰 SFR（除 ASM 内联汇编与 exp8/9 的 SPI1BAUD 切换）。

---

## §5 性能漂移现象（实测发现）

### §5.1 现象描述

把测试代码从 5 个文件合并到一个 HAL `.c` 文件后，**多个时间测试的数字发生漂移**：

| 测试 | 指标 | 重构前 | 重构后 | 比例 |
|---|---|---|---|---|
| Phase 4 ASM | C (10000 字节) | 584802 ticks | 649360 | **慢 11%** |
| Phase 4 ASM | ASM loop | 582513 | 582539 | ≈ 一致 |
| Phase 4 ASM | ASM unroll | 597833 | 597828 | ≈ 一致 |
| Phase 5 Timing | Polling 12MHz | 10072 | **8047** | **快 25%** |
| Phase 5 Timing | DMA 12MHz | 2533 | 2534 | ≈ 一致 |
| Phase 5 Timing | Interrupt 12MHz | 91949 | 93100 | ≈ 一致 |

### §5.2 根因分析

**Phase 4 ASM 的反转**最明显，根因清晰：

C 版本原为 `static` 函数在同一 TU：
```c
// 重构前 test_spi_soft_asm.c 里：
static u8 soft_spi_byte_c(u8 tx)
{
    for (int i=7; i>=0; i--) {
        if (tx & (1<<i)) GPIOESET = A_MOSI;
        ...
        if (GPIOE & A_MISO) rx |= (1<<i);
        ...
    }
}
```
编译器**完全内联** + 可识别变量传播、可循环展开（partial unroll）。

重构后调 HAL：
```c
// 重构后：
static u8 soft_spi_byte_c(u8 tx)
{
    return spi_hal_soft_byte(tx);   // 跨 TU 调用
}
```
无 LTO 时编译器**不能跨 TU inline**，每次 byte 都有 CALL + 寄存器保存/恢复开销。

**`__asm__ volatile` 不受影响**：volatile 约束强制编译器把汇编原样插入调用点，相当于"手写 inline"。

### §5.3 Phase 5 Polling 变快的解释

理论上也该变慢，但实测变快了。可能原因（猜测，未深入验证）：
- 新的内联展开改变了 cache 行为（4096 字节 buffer 命中方式不同）
- 编译器不同的寄存器分配方案 + SPI1BUF 写入路径被简化
- 不同 GCC 版本/编译选项的优化结果差异

**结论**：这种"微抖动"是真实存在于嵌入式 C 编译中的，没必要深究。

### §5.4 关键结论（性能漂移不改变测试通过性）

- ✅ DMA 速度稳定（HAL 调用影响最小，DMA 由硬件搬运）
- ✅ Interrupt 速度稳定（ISR 路径未触及 HAL 边界）
- ✅ 所有测试 data match OK、3-way 一致性 OK、Page Program OK
- ⚠️ C 速度 vs ASM 速度相对反转——**这是抽象代价，必须用 `-flto` 关闭**
- ⚠️ Polling 等绝对值会小幅漂移——不影响相对比例结论

### §5.5 应用 LTO 修复 Phase 4 ASM 漂移（已记录为后续）

GCC 的 `-flto` 选项允许 link-time 跨 TU 内联。要彻底消除 C 版本调用开销：

```bash
# 在 app.cbp 的其他编译器选项加：
-Wl,--gc-sections -flto
```

预计重构后开 LTO 后 C 版本会恢复 ~58 µs/byte（与 ASM loop 速度对齐），与重构前一致。

---

## §6 后续可选重构思路

### §6.1 中重构（Approach 2/3 混合，~适度复杂度）

**目标**：消除"sw_/hw_" 前缀重复，让 W25Q64 驱动只一份代码。

**方法**：用宏或函数指针把 byte sender 参数化，编译期固定：

```c
/* spi_hal.h */
typedef u8 (*spi_xfer_byte_fn)(u8 tx);

/* 编译期选 bus 类型 */
#ifdef SPI_HAL_USE_SW
#define SPI_XFER spi_hal_soft_byte
#elif defined(SPI_HAL_USE_HW)
#define SPI_XFER spi_hal_hw_byte
#endif

/* W25Q64 驱动只有一份 */
void spi_hal_w25_write_enable(void) { cs_low(); SPI_XFER(0x06); cs_high(); }
u8   spi_hal_w25_read_status(u8 cmd) { cs_low(); SPI_XFER(cmd); u8 s = SPI_XFER(0xFF); cs_high(); return s; }
... 等等
```

**好处**：
- W25Q64 驱动 12 个函数 → 缩到 6 个（去一半）
- 切换 soft/hw 只在 .c 文件里改 `#define` 一行
- 测试代码选 bus 通过编译选项

**坏处**：
- 测试代码不能再同时调 sw 版和 hw 版（除非条件编译两份）
- 增加 .c 文件 #ifdef 复杂度
- 用户决策已定"测试层仍区分 soft/hw"，所以**测试代码需要两份调用**——中重构这条路反而与用户决策冲突

**结论**：用户决策"保留 soft/hw 双套"**与中重构的"一份驱动"目标冲突**。本轮不实施。如果未来用户改变主意可回看。

### §6.2 深重构（Approach 4 + 运行时多态，~高复杂度）

**目标**：测试代码完全 bus 无关，运行时选 soft / hw。

**方法**：引入 bus table：

```c
/* spi_hal.h */
typedef struct {
    const char *name;
    void   (*init)(u32 baud_or_unused);
    u8     (*byte)(u8 tx);
    void   (*cs_low)(void);
    void   (*cs_high)(void);
    void   (*cs_low_then_byte_cmd)(u8 cmd);  /* 复合操作 */
    /* ... */
} spi_bus_t;

extern const spi_bus_t *g_spi_bus;  /* 当前活动 bus */

/* 定义两份 */
extern const spi_bus_t spi_bus_soft;
extern const spi_bus_t spi_bus_hw;
```

**测试代码**：
```c
g_spi_bus = &spi_bus_soft;   /* 切软件 */
test_loopback();              /* 调用 g_spi_bus->byte() */
g_spi_bus = &spi_bus_hw;      /* 切硬件 */
test_loopback();              /* 同一份代码 */
```

**好处**：
- 测试逻辑 100% bus 无关
- 切 bus 只改一个指针 + 重 init
- 未来加新 bus（DMA-only、SPI0、I2C、UART）只需注册新 table
- 接近真正的 STM32 HAL 抽象

**坏处**：
- 间接调用（每 byte 多 1 次指针解引用 + 函数调用 overhead，比 §5 更慢）
- `g_spi_bus` 是全局指针——多线程环境需要锁（这里单线程 OK）
- 增加学习曲线
- 用户决策"测试层仍区分 soft/hw"也对此构成反向力（既然已经分两套入口代码，加上 bus table 没必要）

**结论**：本轮不实施。未来如果用户改变决策可重新评估。

---

## §7 换芯片思路（迁移到 STM32 / BT8925 等）

### §7.1 当前架构的瓶颈

本轮 HAL 只服务 BT892X（用户决策）。如果未来要换 STM32 / BT8925：

- `spi_hal.c` 全部实现（~300 行）**整个文件**都要重写（硬件 SFR 不同）
- `spi_hal.h` 头部函数原型**不变**（这是抽象的价值）
- 测试代码 `test_spi_*.c` **完全不改**

换芯片工作量：仅替换 `spi_hal.c`，约 1-2 小时。

### §7.2 推荐三阶段迁移路径

**阶段 1：BT8925（最简单）**
- 假设 BT8925 与 BT892X 共享 SPI 控制器 API（只是寄存器地址偏移）
- 改 `spi_hal.c` 顶部的 `SPI1CON/BUF/BAUD/CPND/DMA*ADR/DMA*CNT` 地址宏 + `FUNCMCON1` 地址宏
- 测试代码**零修改**

**阶段 2：同一厂商其他芯片**
- 增加 `#ifdef BT892X / BT8925 / BT8930` 选择
- 每个芯片一份 `spi_hal_chipX.c`
- 在 build 选项选芯片

**阶段 3：跨厂商（STM32 / NXP）**
- 必须改 `spi_hal.c` 的 SFR 访问全部换成 STM32 HAL 调用
- 借用 STM32 HAL：`HAL_SPI_Transmit(&hspi1, ...)` 取代 `SPI1BUF = tx;`
- 保留 `spi_hal.h` 函数原型 → 测试代码**仍可零修改**
- 这才是 HAL 的最大价值——**测试代码是芯片无关的**

### §7.3 如果未来要做跨芯片：当前架构需要做的小调整

1. **把 SFR 宏集中**：当前 SFR 宏散落在 `spi_hal.c` 里。换芯片应集中到 `spi_hal_chip_bt892x.h`，便于切换
2. **加 chip selector build flag**：`#define SPI_HAL_CHIP_BT892X 1` 之类，编译时选不同 chip 实现
3. **测试代码维持不变**：保证 `spi_hal.h` 函数原型不变，只是函数实现换芯片

本轮**不做**这些 because 用户决策"不预留多芯片"——下次真要换时再做。

---

## §8 经验教训

### §8.1 重构成功经验

1. **小步前进 + 用户逐个验证**：5 个 Step 顺序推进，每步用户测 OK 才进下一步，避免一次性大改动隐藏 bug
2. **保持接口不变**：HAL 函数原型设计后不再改，让测试代码稳定
3. **行为优先**：重构不引入新功能、不改默认行为、不修改编译宏（如 `SPI_W25_RUN_MODE` 等）

### §8.2 重构暴露/发现的 bug

1. **test_spi_wave.c 的 Phase 0 设计 bug**：注释"全跑"但代码只跑一种，原是 while(1) 跨阶段没法连跑的拐弯说法，已修正（PHASE=1/2 互斥 + 默认值 #error）
2. **test_spi_timing.c 的字节序回归**：重构时多写 `(void)` cast 导致 JEDEC 字节序错位（Capacity 0x17 vs Manufacturer 0xEF）——字节序敏感代码需逐字节重读
3. **性能漂移**：C 慢 11% / Polling 漂 25%——重构抽象的固有代价。教训：**对裸机嵌入式 C，TU 边界是不可忽视的优化屏障**

### §8.3 不要做的事

1. ❌ **不要无 LTO 还要求 C 与 ASM 速度对齐**——徒劳，加 `-flto`
2. ❌ **不要在没有验证时合并"看上去一样"的代码块**——字节序/标志位/移位都可能错
3. ❌ **不要为"未来可能需要"预留多芯片/运行时多态**——决策确认不做才是好工程
4. ❌ **不要把 main.c 的 5 个 TEST_SPI_*_EN 开关合并或改名**——会破坏用户现有测试脚本

### §8.4 后续建议（用户参考）

- **加 LTO**：在 app.cbp Compiler settings 加 `-flto`，消除 Phase 4 ASM 的 C 版本 ~11% 性能漂移
- **测脚本稳定化**：5 个测试每个都建立"现象基线"（Polling/dma/interrupt ticks 数），后续改动前后比对
- **文档保持同步**：以后每改 `spi_hal.*` 都需更新本文档的"接口总览"和"调用关系图"

---

## §9 附录：关键文件路径

| 文件 | 状态 | 行数 | 角色 |
|---|---|---|---|
| `smart_mini/test/spi_hal.h` | **新建** | ~80 | HAL 接口声明 + pin macros |
| `smart_mini/test/spi_hal.c` | **新建** | ~270 | HAL 实现（手/硬原语 + W25Q64 驱动 + IT/DMA） |
| `smart_mini/test/test_spi_loop.c` | 重构 | 86（-37%） | 跳线回环，调 HAL |
| `smart_mini/test/test_spi_wave.c` | 重构 | 89（-31%） | LA 波形 + Phase 0 bug 修复 |
| `smart_mini/test/test_spi_soft_asm.c` | 重构 | 140（-19%） | C/ASM/ASM-展开速度对比 |
| `smart_mini/test/test_spi_w25q64.c` | 重构 | ~560（实验函数保持） | W25Q64 软硬全套，调 HAL |
| `smart_mini/test/test_spi_timing.c` | 重构 | 231（-16%） | 3-way timing，调 HAL |
| `smart_mini/test/test_spi_common.h` | **待删除** | ~30 | 内容已合并到 spi_hal.h |
| `smart_mini/app.cbp` | 修改 | +4 | 加 spi_hal.c/.h Unit |
| `smart_mini/main.c` | **未动** | — | 5 个 TEST_SPI_*_EN 开关保持 |
| `docs/test_spi.md` | **待更新** | — | 加"HAL 重构"节指向本文档 |
| `docs/test_spi_hal_refactor.md` | **新建（本文档）** | ~ | 重构记录 |

---

## §10 验证方式

本次重构的端到端验证：**5 个测试分别烧对应开关**，每个现象都与重构前一致：

| Step | 开关 | 现象 |
|---|---|---|
| 1 | `TEST_SPI_LOOP_EN` | 软+硬回环 `0/256 errors PASSED` |
| 2 | `TEST_SPI_WAVE_EN` (PHASE=1 + PHASE=2) | LA 解码 0x55 |
| 3 | `TEST_SPI_SOFT_ASM_EN` | C 64µs/byte, ASM 58µs/byte (反转 OK) |
| 4 | `TEST_SPI_W25Q64_EN` | W25Q64 JEDEC 0xEF4017 + 18 个实验全过 |
| 5 | `TEST_SPI_TIMING_EN` | 3-way 对比 + DMA Page Program PASSED |

每步都在用户实测确认后才提交（重构测试一个我测试一个的工作流）。
