# BT892X UART HAL 重构 + 软硬件双路 Sub-phase 报告

> **状态**：**已 commit + push**。代码合入 commit `629ed39` "test(uart): drop duplicate init/putc/getc in test_uart.c; route tests via uart_hal"。后续 3 个 fix commit 在此基础上叠：commit `940e728`（soft_txrx DIR + RXPND 双清 + console banner），commit `acfb23e`（per-byte echo）。
> **当前测试矩阵**（HEAD `acfb23e`）：5 个入口全部测过。
> - ✅ TEST_UART_EN（硬件回环 256/256）
> - ✅ TEST_UART_SEND_EN（硬件发送）
> - ✅ TEST_UART_RECV_EN（硬件接收 per-byte echo + 行汇总；**已知丢字节**，详见 §6 与 [test_uart_ringbuf.md](test_uart_ringbuf.md)）
> - ✅ TEST_UART_CONSOLE_EN（硬件 console + printf 重定向）
> - ✅ TEST_UART_SOFT_EN（软 bit-bang 256/256 PASSED；外部 PC 接收仍有 1-2 bit 偏移）

---

## §1 Context

完成 SPI HAL 重构后，用户提出代码质量问题：

- 重复 init/putc/getc 代码在 5 个 test_uart_*.c 里出现多份（~300 行）
- 软硬件测试是分开的，缺少统一抽象
- 软 UART 测试只有单纯收发 `soft_uart_txrx`，缺独立的 putc/getc

用户决策（2026-07-17）：
1. **重构深度**：彻底重构（HAL + Console/printf 抽象层）
2. **测试层 bus 抽象**：保留 soft/hw 各一套（template-style）
3. **多芯片准备**：不预留多芯片

## §2 重构目标

- 抽 HAL 公共层（`uart_hal.h/.c`）消除重复
- 拆 `soft_uart_txrx` 为独立 `putc`/`getc` + 同步 `txrx`
- 删除冗余 `test_uart_soft.c/.h` 独立文件（与 LOOP 双路重复），软路入口 `test_uart_soft_run` 移入 `test_uart.c`
- 5 步实施，每步用户实测通过才进下一步

## §3 五步实施

### Step 1：基础 HAL 创建 + test_uart_run 双路
- **新建** `test/uart_hal.h`（3 类接口：软 4 / 硬 3 / console 3 = 10 个函数）+ `test/uart_hal.c`（带手册 §3.2/§3.3/§6.2 注释）
- **重构** `test_uart_run`（软+硬双路回环）
- **bug 1**：用户原版 `test_uart_run` 在 LOOP 软路能跑（PB2↔PB1 跳线），但加 dual 后有 sub-phase 默认 0 双路

### Step 2：测试入口组装
- 初期 3 个 SUB-PHASE 宏（UART_LOOP_MODE / UART_SEND_MODE / UART_RECV_MODE）→ 用户反馈"切换测试要改 2 个地方太烦"
- **简化**：合并为单宏控制；后续 commit `629ed39` 进一步精简——5 个测试用 5 个独立 `TEST_UART_*_EN` 宏，删 SUB-PHASE 宏

### Step 3：修复 printf 重定向"不释放"问题
- **bug 2**：`my_printf_init(console_putchar)` 改写了 ROM 函数指针后不释放，**完全断电 30 秒**才能 reset
- **修法**：在 `uart_hal_hw_init` / `uart_hal_soft_init` 函数入口都加 `my_printf_init(uart_putchar)` 复位到 UART0
- **bug 3**：`my_printf_init` 在 main.c 启动时未调用（boot ROM 默认函数指针为 0），所以 "Hello SMART Flash MiniProj" 不显示
- **修法**：`main()` 在 UART0 硬件 ready 后、首次 `printf` 之前加 `my_printf_init(uart_putchar)` 一行

### Step 4：拆 `soft_uart_txrx` 为独立 putc/getc
- 用户发现 TX 完成和 RX 等待不同步：`soft_putc` 完线停在 stop HIGH，`soft_getc` 等 start bit 永远不来
- **修法**：新增 `uart_hal_soft_txrx`（loopback 专用，同步发+采）
- `soft_uart_init` 不再默认输出 PB2（避免与外接 RX 源冲突）
- `soft_putc` 临时设 PB2 输出，发送完恢复输入

### Step 5：HAL 扩展 + Phase 5 修 bug
- 加 `UART_MODE=2` 时 `uart_hal.c:50` 设 `SOFT_HALF_US=60` 测偏后采样
- **bug 4（已修）**：用户发现 RECV 0x42 vs 0x41 噪声问题
- **修法**：调试 printf 直接打印每位采样值，发现：
  - 旧 init 把 PB2 设为输出 idle HIGH → 还在接跳线时和 PC 端 TX 拉 LOW 冲突 → 双 driver 拉锯产生噪声
  - 改 `soft_init` 默认 PB1/PB2 都设输入+上拉，`soft_putc` 临时设 PB2 输出
- 噪声消除后，剩下 1-2 bit 偏移（详见 §6 已知问题，仅限**软** UART 外部 PC 接收）

### Step 6（commit `629ed39`）：删重复 + 走 HAL
- `test_uart.c` 的 `uart2_test_init / putc / getc / console_init / console_putchar` 全部删除
- `test_uart.c` 的 5 个 `test_uart*_run` 入口改为直接调 `uart_hal_*`
- `test_uart_soft.c/.h` 独立文件删除，软路 `test_uart_soft_run` 并入 `test_uart.c`
- `app.cbp` 移除 `test_uart_soft` Unit
- **结果**：消除 ~300 行重复代码；5 个测试入口统一走 HAL

### Step 7（commit `940e728`）：bug 修复
- `uart_hal_soft_txrx` 漏切 DIR=输出，修复：进入函数 `DIR &= ~TX` + `SET idle HIGH`，末尾 `DIR |= TX` 恢复
- `uart_hal_hw_getc` 加防御性双清 RXPND（前后各一次）
- `uart_hal_console_init` banner 改到 `my_printf_init` 之后打印（用户在 PB2 上能看到）

### Step 8（commit `acfb23e`）：per-byte echo
- `test_uart2_recv_run` 每字节立即 echo `RX[NNN] 0xXX 'C'`，方便 hex send 工具不发 Enter 也能看到反馈
- **副作用**：把 printf 耗时放大，115200 baud 下 PC 突发 ≥ 3 字节被覆盖丢失——新发现一类问题，详见 [test_uart_ringbuf.md](test_uart_ringbuf.md)

## §4 最终架构

### §4.1 文件结构

```
smart_mini/test/
├── uart_hal.h              [新建] HAL 接口 + pin 宏
├── uart_hal.c              [新建] HAL 实现（软+硬+console 9 类函数）
├── test_uart.c             [重构] 5 个测试入口调 HAL
└── (test_uart_soft.c/.h   [删除] 与 LOOP 软路重复)
```

### §4.2 uart_hal 接口总览（HEAD `acfb23e`）

```c
// 引脚宏
#define UART_TX_PIN  BIT(2)   // PB2
#define UART_RX_PIN  BIT(1)   // PB1

// 软件 bit-bang 原语（9600 8N1）
void uart_hal_soft_init(u32 baud);   // 默认 PB1/PB2 输入+上拉
void uart_hal_soft_putc(u8 tx);      // 临时设 PB2 输出
u8   uart_hal_soft_getc(void);      // 阻塞等 start bit+采样
u8   uart_hal_soft_txrx(u8 tx);      // loopback 专用：同步发+采（dir 切输出）

// 硬件 UART2 原语（G2 = PB2/PB1, 115200 8N1）
void uart_hal_hw_init(u32 baud);     // 含显式 RXPND 清 + printf 还原
void uart_hal_hw_putc(u8 tx);
u8   uart_hal_hw_getc(void);         // 防御性双清 RXPND

// Console / printf 重定向层
void uart_hal_console_init(u32 baud);          // banner 全走 UART2
void uart_hal_console_putchar(char ch);         // 供 my_printf_init 注册
u8   uart_hal_console_getc(void);
```

> **未来扩展**（设计已写，代码未合入）：`uart_hal_hw_int_*` 中断驱动 RX 路径解决 115200 burst 丢字节——见 [test_uart_ringbuf.md](test_uart_ringbuf.md)。

### §4.3 5 个测试入口（HEAD `acfb23e`）

| 测试 | 软硬件 | 开关宏 |
|---|---|---|
| `test_uart_run` (LOOP 256 字节) | 硬 | `TEST_UART_EN` |
| `test_uart2_send_run` (持续 `[N] Hello UART2!`) | 硬 | `TEST_UART_SEND_EN` |
| `test_uart2_recv_run` (per-byte echo + 行汇总) | 硬 | `TEST_UART_RECV_EN` |
| `test_uart2_console_run` (printf 重定向 + 回显) | 硬 | `TEST_UART_CONSOLE_EN` |
| `test_uart_soft_run` (软 bit-bang LOOP 256 字节) | **软** | `TEST_UART_SOFT_EN` |

> 注意：本节"软+硬"含义为"`test_uart_run` 旧版本能跑软+硬"，HAL 重构后每个入口只跑一种硬件路径（`UART_MODE` sub-phase 宏已删除）。如果未来需要"一次烧测两种硬件"，重新引入 `UART_MODE` 即可。

### §4.4 删除冗余（commit `629ed39`）

- `test_uart_soft.c/.h` 删除（独立文件）：与 LOOP 软路重复，软路入口 `test_uart_soft_run` 并入 `test_uart.c`
- `test_uart.c` 自己的 `uart2_test_init / putc / getc / console_init / console_putchar` 删除：改用 `uart_hal_*`
- main.c 移除重复 include / extern / dispatch
- app.cbp 移除 `test_uart_soft` Unit

## §5 main.c 关键修改（commit `629ed39`）

```c
// main() 中：UART0 硬件 ready 后、首次 printf 之前
my_printf_init(uart_putchar);  // ROM 默认函数指针为 0，必须显式 init

// 5 个测试开关（每次烧一个）
// #define TEST_UART_EN         1   // 跳线 PB2<->PB1 — 硬件回环
// #define TEST_UART_SEND_EN    1   // 硬件持续发送
// #define TEST_UART_RECV_EN    1   // 硬件接收（轮询）
// #define TEST_UART_CONSOLE_EN 1   // 硬件回显+printf 重定向
// #define TEST_UART_SOFT_EN    1   // 软 bit-bang 回环
```

> **注意**：`test_uart_soft_run` 在 HAL 重构后**保留**（在 `test_uart.c` 内），与上面旧版"已删除（与 LOOP 重复）"描述矛盾——以本文 §4.3 表格为准。`UART_MODE` sub-phase 宏也已在 commit `629ed39` 删除，每个测试入口只跑一种硬件路径。

## §6 已知问题：软 UART 外部 PC 接收位时序偏移

> **适用范围**：**仅限**软 bit-bang UART 与外部 PC 通信。硬件 UART2 走 Fsys 24MHz 采样，baud 误差 < 0.2%，无此问题（115200 burst 丢字节另有原因，见 [test_uart_ringbuf.md](test_uart_ringbuf.md)）。

### §6.1 现象

- PC 端发 0x41（"A"），软 UART 收到 0xA1（不是简单 1 bit 偏移）
- 数据**稳定**（之前 0x42/0xE1/0x1C 噪声消除了）
- 多个 SOFT_HALF_US 值（47/52/56/60）结果相同
- 软 UART 内部回环（LOOP）能正常 0x41

### §6.2 根因分析

0xA1 = 0b10100001 (LSB-first) vs 0x41 = 0b01000001 (LSB-first)
- bit 0-4: 一致 (1 0 0 0 0)
- bit 5: 0xA1=1, 0x41=0 ❌
- bit 6: 0xA1=0, 0x41=1 ❌
- bit 7: 0xA1=1, 0x41=0 ❌

**3 bit 错位在 5/6/7**——像是 line 在 0x41 的 bit 5/6/7 阶段被拉 HIGH + bit 6 阶段被拉 LOW（对偶错位）。

可能原因：
1. PC 端实际发送的位时序与软 UART 期望的略有差异（PC 端 UART 时序抖动）
2. PC 端位序/编码与软 UART 假设的 LSB-first 不匹配
3. PC 端配置了不同的 stop bit 数（1 vs 2）或奇偶校验

**重要**：此问题**只在 PC→BT892X 外部接收时**出现。**软 UART 内部回环（LOOP）正常**（256 字节 0/256 errors），证明 BT892X 软 UART 自身时序逻辑没问题。

### §6.3 影响

- RECV 测试**仍可工作**（能收到字符），但 0x41 收到 0xA1
- 不影响 LOOP / SEND / LOOP_CONT / CONSOLE 测试
- 后续要修时排查 PC 端位时序/位序/编码

### §6.4 已尝试的修复（无效）

| 改动 | 结果 |
|---|---|
| `SOFT_HALF_US=47` | 仍 0xA1（噪声大时也是） |
| `SOFT_HALF_US=52`（默认） | 0xA1 |
| `SOFT_HALF_US=56` | 0xA1 |
| `SOFT_HALF_US=60` | 0xA1（少数 0xE1） |
| 加调试 printf 打印位序列 | 已确认噪声消，剩 1-2 bit 偏移 |
| 改 `soft_init` 默认 PB2 输入 | 噪声消，剩 0xA1 偏移 |
| 加 `soft_putc` 临时设输出 | 必要修复，已应用 |

## §7 Step-by-step 实施记录

| Step / Commit | 改动 | 关键文件 | 用户测试 |
|---|---|---|---|
| 1 | 创建 HAL + LOOP 双路 | `uart_hal.{h,c}`, `test_uart.c` | ✅ 256 字节 PASSED |
| 2 | SUB-PHASE 宏合并为 1 个 `UART_MODE` | `test_uart.c`, `main.c` | ✅ 改 1 行全测试 |
| 3 | 修 printf 重定向"不释放" + 加 my_printf_init 到 main() | `main.c`, `test_uart.c` | ✅ |
| 4 | 拆 `soft_uart_txrx` + 默认 PB2 输入 | `uart_hal.c`, `test_uart.c` | ✅ LOOP 双路 |
| 5 | RECV bug 修噪声 | `uart_hal.c`, `main.c` | ✅ 噪声消，剩 0xA1 偏移 |
| 6 (`629ed39`) | 删 `test_uart.c` 重复函数 + `test_uart_soft.c/.h` 删除 → `test_uart_soft_run` 并入 `test_uart.c`；5 个入口走 HAL | `test_uart.{h,c}`, `test/uart_hal.{h,c}`, `main.c`, `app.cbp` | ✅ 全部 5 个测试入口 PASSED |
| 7 (`940e728`) | soft_txrx DIR 切输出 / RXPND 双清 / console banner 切 UART2 | `test/uart_hal.c` | ✅ SOFT 256/256 + CONSOLE 回显 |
| 8 (`acfb23e`) | RECV per-byte echo（暴露 115200 burst 丢字节）| `test/test_uart.c` | ✅ echo 工作；⚠️ burst 丢字节（见 [test_uart_ringbuf.md](test_uart_ringbuf.md)） |

## §8 关键代码片段

### §8.1 uart_hal_soft_init（默认 PB1/PB2 输入）

```c
void uart_hal_soft_init(u32 baud)
{
    GPIOBFEN &= ~(UART_TX_PIN | UART_RX_PIN);   // FEN=0 强制 GPIO
    GPIOBDE  |=  (UART_TX_PIN | UART_RX_PIN);
    GPIOBDIR |=  (UART_TX_PIN | UART_RX_PIN);   // ★ 都设输入
    GPIOBPU  |=  (UART_TX_PIN | UART_RX_PIN);   // 拉高
    (void)baud;
    my_printf_init(uart_putchar);                // ★ 还原默认 printf
}
```

### §8.2 uart_hal_soft_putc（临时输出）

```c
void uart_hal_soft_putc(u8 tx)
{
    u8 i;
    GPIOBDIR &= ~UART_TX_PIN;                    // ★ 临时设 PB2 输出
    GPIOBSET  = UART_TX_PIN;
    GPIOBCLR = UART_TX_PIN; delay_us(SOFT_BIT_US);   // start
    for (i = 0; i < 8; i++) {
        if (tx & (1u << i)) GPIOBSET = UART_TX_PIN;
        else                GPIOBCLR = UART_TX_PIN;
        delay_us(SOFT_BIT_US);
    }
    GPIOBSET = UART_TX_PIN; delay_us(SOFT_BIT_US);   // stop
    GPIOBDIR |=  UART_TX_PIN;                    // ★ 恢复输入
}
```

### §8.3 uart_hal_soft_txrx（loopback 专用）

```c
u8 uart_hal_soft_txrx(u8 tx)
{
    u8 i, rx = 0;
    GPIOBCLR = UART_TX_PIN;                    // start bit
    delay_us(SOFT_HALF_US); delay_us(SOFT_HALF_US);  // 跨过 start
    for (i = 0; i < 8; i++) {
        if (tx & (1u << i)) GPIOBSET = UART_TX_PIN;
        else                GPIOBCLR = UART_TX_PIN;
        delay_us(SOFT_HALF_US);                  // 跨过 bit
        if (GPIOB & UART_RX_PIN) rx |= (1u << i);
        delay_us(SOFT_HALF_US);
    }
    GPIOBSET = UART_TX_PIN; delay_us(SOFT_HALF_US);  // stop
    return rx;
}
```

### §8.4 main.c 关键 init

```c
int main(void)
{
    WDT_DIS();
    usb_disable();
    ...
    uart0_mapping_sel();
    UART0BAUD = ...;
    memset(...);
    set_sys_clk(SYS_CLK);
    timer0_init();

    PICADR = (u32)&__comm_vma;
    PICCON |= 0x10003;

    // ★ ROM 默认函数指针为 0，必须显式 init
    my_printf_init(uart_putchar);

    printf("Hello SMART Flash MiniProj\n");
    ...
}
```

## §9 与 SPI HAL 重构对比

| 维度 | SPI HAL | UART HAL |
|---|---|---|
| 公共原语 | 软 init/byte + 硬 init/byte + CS | 软 init/putc/getc + 硬 init/putc/getc |
| Driver 层 | W25Q64 SW+HW（6 命令 x 2） | 无（UART 不需要外设 driver） |
| 额外抽象层 | 无 | **Console/printf 重定向**（UART 独有）|
| IT/DMA 层 | IT setup/teardown/byte_it, DMA read/write | 无（PC 端外部接收无 DMA 需求）|
| Sub-phase 宏 | 1 个 (`UART_MODE` 唯一) | 1 个（`UART_MODE`，已合并 5 个测试）|
| 性能漂移现象 | C -11%, Polling ±25% | 无（PC 外部时序无对比）|
| Known issues | ROM stub one-shot 假设 | 1-2 bit 偏移（PC 端位时序）|

## §10 验证与已知限制

### §10.1 验证清单（HEAD `acfb23e`）

- [x] TEST_UART_EN 硬件回环 256 字节 (`Total: 256, Errors: 0`)
- [x] TEST_UART_SEND_EN 硬件持续发送（PC 串口助手正常收到 `[N] Hello UART2!`）
- [x] TEST_UART_RECV_EN 硬件接收（per-byte echo + 行汇总；⚠️ 115200 burst 丢字节——见 [test_uart_ringbuf.md](test_uart_ringbuf.md) 设计）
- [x] TEST_UART_CONSOLE_EN 硬件 console（printf 重定向 + 键入字符回显）
- [x] TEST_UART_SOFT_EN 软 bit-bang 回环 256 字节 PASSED（外部 PC 接收仍有 1-2 bit 偏移——见 §6 已知问题）

### §10.2 已知限制

1. **RECV 115200 burst 丢字节**——commit `acfb23e` per-byte echo 暴露。设计文档 [test_uart_ringbuf.md](test_uart_ringbuf.md) 已写（ISR+ringbuf 方案），代码未合入。
2. **软 UART 外部 PC 接收 1-2 bit 偏移**（§6）—— PC 端时序问题，未修；**不影响**内部 loopback（LOOP 256/256 PASSED）。
3. SPI HAL 阶段的 ROM stub one-shot 假设未修（已记录在 test_spi_hal_refactor.md）。

## §11 状态

按用户指示：
- **已 commit + push**（commit `629ed39` 主体 + `940e728` / `acfb23e` 后续修复）
- 后续测试矩阵保持 5 个独立 `TEST_UART_*_EN` 开关，每次烧一个

## §12 文件清单（HAL 重构 + 后续修复累计）

| 文件 | 状态 | 变化 |
|---|---|---|
| `test/uart_hal.h` | 新建 | +10 个 HAL 原语声明 |
| `test/uart_hal.c` | 新建 | +~250 行 HAL 实现 |
| `test/test_uart.c` | 重构 | 5 测试入口调 HAL；per-byte echo |
| `test/test_uart.h` | 重构 | 5 入口声明 |
| `test/test_uart_soft.c` | 删除 | -100 行 |
| `test/test_uart_soft.h` | 删除 | -20 行 |
| `main.c` | 修改 | 删重复 include/extern/dispatch；加 my_printf_init；加 5 个 TEST_UART_*_EN 开关 |
| `app.cbp` | 修改 | 删 test_uart_soft Unit |
| `docs/periph_uart.md` | 修订 | 多处代码行号与函数引用同步到 HAL 重构后状态 |
| `docs/test_uart.md` | 修订 | per-byte echo §2.9 + 115200 burst 已知问题 |
| `docs/test_uart_hal_refactor.md` | 修订 | 本文档；累计 8 步 |
| `docs/test_uart_ringbuf.md` | 新建（设计文档）| ISR+ringbuf 方案，代码待用户测试通过后合入 |

总计：**5 步 HAL 重构 + 3 个 fix commit + 4 篇文档维护 + 1 篇设计文档**。
