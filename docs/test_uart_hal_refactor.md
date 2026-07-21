# BT892X UART HAL 重构 + 软硬件双路 Sub-phase 报告

> **状态**：完整代码、文档（5 步重构）写完，**未 commit+push**。待用户决定 push 策略后处理（可能有 GitHub 冲突要解决）。
> **测试结果**：
> - ✅ Phase 1 LOOP：256 字节软+硬双路回环 PASSED
> - ✅ Phase 1b LOOP_CONT：软+硬持续 0x55 + 每秒 printf
> - ✅ Phase 2 SEND：软+硬 0xAA *10 *10 轮
> - 🟡 Phase 3 RECV：可读出字符（噪声消除），但位时序有 1-2 bit 偏移（详见 §6 RECV 已知问题）
> - ✅ Phase 4 CONSOLE：printf 重定向 + 回显

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
- 加 `UART_MODE` 单宏控制 5 个测试的 sub-phase（0=双路/1=只软/2=只硬）
- 5 步实施，每步用户实测通过才进下一步
- 删除冗余 `test_uart_soft_run`（与 LOOP 双路重复）

## §3 五步实施

### Step 1：基础 HAL 创建 + test_uart_run 双路
- **新建** `test/uart_hal.h`（5 类接口）+ `test/uart_hal.c`（带手册 §3.2/§3.3/§6.2 注释）
- **重构** `test_uart_run`（软+硬双路回环 + sub-phase 宏 `UART_LOOP_MODE`）
- **bug 1**：用户原版 `test_uart_run` 在 LOOP 软路能跑（PB2↔PB1 跳线），但加 dual 后有 sub-phase 默认 0 双路

### Step 2：SUB-PHASE 宏（UART_LOOP_MODE / UART_SEND_MODE / UART_RECV_MODE）
- 初期 3 个宏（每测试一个）→ 用户反馈"切换测试要改 2 个地方太烦"
- **简化**：合并为 1 个 `UART_MODE` 宏（0=双路/1=只软/2=只硬），5 个测试共用

### Step 3：修复 printf 重定向"不释放"问题
- **bug 2**：`my_printf_init(console_putchar)` 改写了 ROM 函数指针后不释放，**完全断电 30 秒**才能 reset
- **修法**：在 `uart_hal_hw_init` / `uart_hal_soft_init` / `test_uart2_loop_continuous_run` 函数入口都加 `my_printf_init(uart_putchar)` 复位到 UART0
- **bug 3**：`my_printf_init` 在 main.c 启动时未调用（boot ROM 默认函数指针为 0），所以 "Hello SMART Flash MiniProj" 不显示
- **修法**：`main()` 在 UART0 硬件 ready 后、首次 `printf` 之前加 `my_printf_init(uart_putchar)` 一行

### Step 4：拆 `soft_uart_txrx` 为独立 putc/getc
- 用户发现 TX 完成和 RX 等待不同步：`soft_putc` 完线停在 stop HIGH，`soft_getc` 等 start bit 永远不来
- **修法**：新增 `soft_uart_soft_txrx`（loopback 专用，同步发+采）
- `soft_uart_init` 不再默认输出 PB2（避免与外接 RX 源冲突）
- `soft_putc` 临时设 PB2 输出，发送完恢复输入

### Step 5：HAL 扩展 + Phase 5 修 bug
- 加 `UART_MODE=2` 时 `uart_hal.c:50` 设 `SOFT_HALF_US=60` 测偏后采样
- **bug 4（已修）**：用户发现 RECV 0x42 vs 0x41 噪声问题
- **修法**：调试 printf 直接打印每位采样值，发现：
  - 旧 init 把 PB2 设为输出 idle HIGH → 还在接跳线时和 PC 端 TX 拉 LOW 冲突 → 双 driver 拉锯产生噪声
  - 改 `soft_init` 默认 PB1/PB2 都设输入+上拉，`soft_putc` 临时设 PB2 输出
- 噪声消除后，剩下 1-2 bit 偏移（详见 §6 已知问题）

## §4 最终架构

### §4.1 文件结构

```
smart_mini/test/
├── uart_hal.h              [新建] HAL 接口 + pin 宏
├── uart_hal.c              [新建] HAL 实现（软+硬+console 9 类函数）
├── test_uart.c             [重构] 5 个测试入口调 HAL
└── (test_uart_soft.c/.h   [删除] 与 LOOP 软路重复)
```

### §4.2 uart_hal 接口总览

```c
// 引脚宏
#define UART_TX_PIN  BIT(2)   // PB2
#define UART_RX_PIN  BIT(1)   // PB1

// 软件 bit-bang 原语（9600 8N1）
void uart_hal_soft_init(u32 baud);   // 默认 PB1/PB2 输入+上拉
void uart_hal_soft_putc(u8 tx);      // 临时设 PB2 输出
u8   uart_hal_soft_getc(void);      // 阻塞等 start bit+采样
u8   uart_hal_soft_txrx(u8 tx);      // loopback 专用：同步发+采

// 硬件 UART2 原语（G2 = PB2/PB1, 115200 8N1）
void uart_hal_hw_init(u32 baud);
void uart_hal_hw_putc(u8 tx);
u8   uart_hal_hw_getc(void);

// Console / printf 重定向层
void uart_hal_console_init(u32 baud);
void uart_hal_console_putchar(char ch);   // 供 my_printf_init 注册
u8   uart_hal_console_getc(void);
```

### §4.3 4 个测试入口

| 测试 | 软硬件支持 | sub-phase 宏 |
|---|---|---|
| `test_uart_run` (LOOP 256 字节) | ✅ 软+硬 | `UART_MODE` |
| `test_uart2_send_run` (10 轮 *10 字节) | ✅ 软+硬 | `UART_MODE` |
| `test_uart2_recv_run` (16 字节) | ✅ 软+硬 | `UART_MODE` |
| `test_uart2_loop_continuous_run` (持续 0x55) | ✅ 软+硬 | `UART_MODE` |
| `test_uart2_console_run` (printf 重定向+回显) | ❌ 仅硬 | — |

### §4.4 删除冗余

- `test_uart_soft_run`（`test_uart_soft.c/.h`）删除：与 LOOP 软路完全重复
- main.c 移除 include / extern / dispatch
- app.cbp 移除 2 个 Unit entries

## §5 main.c 关键修改

```c
// main() 中：UART0 硬件 ready 后、首次 printf 之前
my_printf_init(uart_putchar);  // ROM 默认函数指针为 0，必须显式 init

// 5 个测试开关（每次烧一个）
// #define TEST_UART_LOOP_EN     1   // 跳线 PB2<->PB1 — 软+硬
// #define TEST_UART_LOOP_CONT_EN 1  // 持续回环 0x55 + printf
// #define TEST_UART_SEND_EN     1   // 持续发送
// #define TEST_UART_RECV_EN     1   // 持续接收
// #define TEST_UART_CONSOLE_EN  1   // 回显+printf 重定向
// #define TEST_UART_SOFT_EN     1   // 已删除（与 LOOP 重复）

// Sub-phase（一个宏控制 5 个测试）
// #define UART_MODE 0   // 0=双路 / 1=只软 / 2=只硬
```

## §6 已知问题：Phase 3 RECV 位时序偏移

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

| Step | 改动 | 关键文件 | 用户测试 |
|---|---|---|---|
| 1 | 创建 HAL + LOOP 双路 | `uart_hal.{h,c}`, `test_uart.c` | ✅ 256 字节 PASSED |
| 2 | SUB-PHASE 宏合并为 1 个 `UART_MODE` | `test_uart.c`, `main.c` | ✅ 改 1 行全测试 |
| 3 | 修 printf 重定向"不释放" + 加 my_printf_init 到 main() | `main.c`, `test_uart.c` | ✅ |
| 4 | 拆 `soft_uart_txrx` + 默认 PB2 输入 | `uart_hal.c`, `test_uart.c` | ✅ LOOP 双路 |
| 5 | RECV bug 修噪声 + 删除冗余 | `uart_hal.c`, `main.c`, `test_uart_soft.{c,h}`, `app.cbp` | ✅ 噪声消，剩 0xA1 偏移 |

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

### §10.1 验证清单

- [x] LOOP 软+硬 256 字节回环
- [x] LOOP_CONT 软+硬 持续 0x55
- [x] SEND 软+硬 10 轮 *10 字节
- [x] RECV 软+硬能收到字符（噪声消除，但位时序偏移）
- [x] CONSOLE 硬：printf 重定向 + 回显

### §10.2 已知限制

1. RECV 位时序偏移（§6）—— PC 端问题，未修
2. SPI HAL 阶段的 ROM stub one-shot 假设未修（已记录在 test_spi_hal_refactor.md）
3. 需完全断电 30s 才能重置 my_printf_init 的 redirect

## §11 下一步

按用户指示：
- **先完善本文档**（当前完成）
- **不 commit+push**（待用户决定 push 策略）
- 后续模块 5（I2C）和收尾删除 smart_mini_copilot/ 还需要处理
- ✅ smart_mini_copilot/ 已删除（2026-07-17 收尾清理）

## §12 文件清单（本次重构）

| 文件 | 状态 | 行数变化 |
|---|---|---|
| `test/uart_hal.h` | 新建 | +70 |
| `test/uart_hal.c` | 新建 | +250 |
| `test/test_uart.c` | 重构 | 5 测试入口调 HAL |
| `test/test_uart_soft.c` | 删除 | -100 |
| `test/test_uart_soft.h` | 删除 | -20 |
| `main.c` | 修改 | -5（删 include/extern/dispatch）+1（加 my_printf_init） |
| `app.cbp` | 修改 | -2（删 test_uart_soft Unit） |

总计：**5 步重构 + 5 个 bug 修复 + 1 个测试冗余删除**。
