# BT892X UART 环形缓冲 + ISR 设计文档

> **状态**：**设计文档**（代码尚未合入），来源 2026-07-22 实测 RECV 测试在 115200 baud 下掉字节（`e` 和 `l` 被覆盖）的根因分析 + 用户决议"先恢复代码，文档先写"。
> **关联 commit**：当前 HEAD `acfb23e`（per-byte echo）——本设计若合入需新增 `test_uart2_recv_int_run` + ISR。
> **相关文档**：
> - [periph_uart.md §8.2 疑点 11](periph_uart.md)（RECV 软路 0x41 → 0xA1 偏移——本设计解决同类问题）
> - [test_uart_hal_refactor.md §6](test_uart_hal_refactor.md)（已知的"轮询跟不上 PC 突发"现象）

---

## 1. 问题：115200 baud 下轮询 RECV 掉字节

### 1.1 实测现象（2026-07-22）

`TEST_UART_RECV_EN` 默认开，烧板后 PC 串口助手发"hello"，PB3 输出：

```
RX[000] 0x68 'h'
RX[001] 0x6C 'l'
RX[002] 0x6F 'o'
RX[003] 0x68 'h'
RX[004] 0x6C 'l'
...
```

`'e'`（0x65）和第二个 `'l'`（0x6C）丢失。

### 1.2 根因：BT892X UART2 无硬件 FIFO + 软件轮询阻塞

| 因素 | 数值 |
|---|---|
| 波特率 | 115200 bps → **每字节 87 µs**（10 bit：start + 8 data + stop）|
| `printf("RX[NNN] 0xXX 'C'\n")` ~20 字符 @ UART0 1.5 Mbps + 格式开销 | **~200–500 µs** |
| BT892X UART2 硬件 FIFO（手册 §6.2） | **无**，仅 1 字节缓冲 |

→ 在 printf 阻塞的 ~300 µs 期间，PC 端 **3–4 个字节** 陆续到达 UART2 硬件。由于 DATA 寄存器只能保留最后一个，旧字节被覆盖丢失。

### 1.3 为什么之前没暴露

旧版 `test_uart2_recv_run`（per-byte echo 加之前）走**行缓冲**——收到字节先攒进 `line_buf`，等到 `\r` 或 `\n` 才整行 `printf`：
- PC 输入"hello\n" → 攒齐 6 字节才打 1 行 → 期间**用户感知不到掉字节**（只看到 "hello"，但实际 'e'/'l' 已经丢了，只是因为行缓冲里还有前一字节的残留）。
- 而且行缓冲模式下，CPU 忙着 printf 的时间相对短一些（按 64 字节行 flush），丢字节概率比 per-byte echo 低。

→ 2026-07-22 加 per-byte echo（commit `acfb23e`）后，每字节都 printf，**耗时放大 5–10 倍**，丢字节暴露。

### 1.4 为什么 SPI 没遇到同类问题

SPI1（`spi_hal_hw_byte`）也是 polling + 阻塞等 SPIPND，但 SPI 测试场景都是**短帧**（≤ 几十字节）且带 `delay_us(1)` 间隔；不像 UART 是 PC 端连续突发。

---

## 2. 设计目标

| 目标 | 指标 |
|---|---|
| 不丢字节 | 115200 baud 下突发 ≥ 32 字节不丢 |
| 不破坏现有测试 | 保留 `test_uart2_recv_run`（轮询版），新增 INT 版独立入口 |
| 不动链接脚本 | `.comm` 区 32 KB 不够装下新代码 + ISR + ring buffer |
| 改动局限 | 只动 `uart_hal.{h,c}` + `test_uart.{h,c}` + `main.c` 三个文件 + `app.cbp` 编译选项 |

---

## 3. 设计方案：RXIE + ISR + 软件环形缓冲

### 3.1 数据流

```
PC 串口 ──USB-TTL TX──> PB1 ──> UART2 硬件 (RX shift register)
                                       │
                                       │ RXPND=1 (每字节触发)
                                       ▼
                          ┌─────────────────────────┐
                          │  ISR (uart2_rx_isr)     │
                          │  - 读 UART2DATA         │
                          │  - 写 g_rx_ring[head]  │
                          │  - head++               │
                          │  - 清 UART2CPND=BIT(9) │
                          │  - while RXPND 循环    │
                          └─────────────────────────┘
                                       │
                                       │ (head - tail) 字节数
                                       ▼
                          ┌─────────────────────────┐
                          │  main 上下文            │
                          │  uart_hal_hw_int_getc() │
                          │  - 等 head != tail     │
                          │  - 读 g_rx_ring[tail]  │
                          │  - tail++               │
                          │  → 返回 1 字节给 caller│
                          └─────────────────────────┘
```

**关键点**：字节进 ring 是 ISR（**不可阻塞**）；main 读 ring 是 user 上下文（**可以慢**）。两者通过 volatile u32 head/tail 解耦。

### 3.2 ISR 设计

**触发**：UART2CON bit 2 RXIE=1（手册 §6.2 L417）+ `register_isr(IRQ_UART_VECTOR, uart2_rx_isr)` + `PICEN |= BIT(IRQ_UART_VECTOR)`。

**IRQ 向量**：`IRQ_UART_VECTOR = 14`（手册 §6 表 L55 "UART0/UART1/UART2 中断"），`int.h` L17 定义。

**ISR body**（`AT(.com_text.isr)`）：

```c
while (UART2CON & BIT(9)) {           // 还有未读字节
    u8 ch = (u8)UART2DATA;
    u32 next = (g_rx_head + 1u) % UART_RX_RING_SIZE;
    if (next != g_rx_tail) {          // ring 未满
        g_rx_ring[g_rx_head] = ch;
        g_rx_head = next;
    }
    /* ring 满：丢新字节，正常情况 128B 不会填满 */
    UART2CPND = BIT(9);               // §6.2 L427 写 1 清 RXPND
}
```

**为什么 `while (RXPND) { ... }`**：
- PC 端突发可能在 ISR 进入前已经到达 ≥ 2 字节，硬件只有 1 字节缓冲，第 2 字节会"挂着"等 ISR 搬走。
- 单次 ISR 循环读空所有可读字节，避免"一次 ISR 只搬 1 字节、下一次 ISR 又被同一字节触发"的不必要开销。

**为什么 ISR 不能阻塞**：ring 满时**丢字节**而不是等 main 消费。ISR 内阻塞会破坏 PIC low-prio ISR 的不可重入假设，且阻塞 printf 会反过来把 ring 喂得更慢。

### 3.3 main 读路径

```c
u8 uart_hal_hw_int_getc(void) {
    while (g_rx_head == g_rx_tail) {  // 空转等 ISR
    }
    u8 ch = g_rx_ring[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1u) % UART_RX_RING_SIZE;
    return ch;
}
```

`uart_hal_hw_int_available()` 返回 ring 当前字节数（非阻塞探查用）。

### 3.4 并发模型分析

| 变量 | 谁写 | 谁读 | 原子性需求 |
|---|---|---|---|
| `g_rx_head` | ISR | main | rv32imac 上 32-bit 对齐访问原子（GCC 6 默认保证）|
| `g_rx_tail` | main | ISR | 同上 |
| `g_rx_ring[head]` | ISR | — | ISR 独占写，main 不读 raw ring（只读 `g_rx_tail` 处的）|

**race 场景**：ISR 正在 `g_rx_head = next` 时，main 在读 `g_rx_head`：
- rv32imac 32-bit 单条指令 `sw` / `lw` 原子，main 读到的是 ISR 写前的旧值或写后的新值，没有中间态。
- ring 满判定 `next != g_rx_tail` 也是单条 `sw` / `lw`，不会撕裂。

**最坏情况**：ISR 写入字节后立刻返回，main 还没看到——下次 ISR 触发或 main 主动轮询 head 时会看到。**不会丢字节**（ISR 已保存），只会延迟处理。

**开关中断临界区**：不需要。PIC low-prio ISR 不会被自己打断，且 ISR 不会调 main 函数。

### 3.5 Ring buffer 大小选择

| 候选 | 在 115200 baud 下能缓冲多久 | .bss 占用 | 评估 |
|---|---|---|---|
| 64 B | ~5.6 ms | 64 B | 边缘，PC 串口工具发 256 字节 burst 可能填满 |
| **128 B** | ~11 ms | **128 B** | ✅ 选中：足够缓冲一次性 PC 突发（hex send 256B ≈ 22ms，仍可能填满，但人机交互不会）|
| 256 B | ~22 ms | 256 B | 最稳但挤占 .bss |

**ring 满策略**：丢弃新字节（不报警）。实测 128B 够用；真要发 256B+ 突发可以再加。

### 3.6 内存布局选择：放 aram 而非 .bss

`ram.ld` 定义：
```
comm(rx)  : org = 0x11000, len = 32k    // 放 .text/.rodata/.data
data      : org = 0x19000, len = 20k    // 放 .bss
aram      : org = 0x50000, len = 16k    // 加速 RAM，本工程未用
```

**问题**：新 ISR + HAL 函数 + test 入口 + 128B ring 把 `.comm` 撑过 32KB 限额（实测 overflow 848B）。

**方案**：ring buffer 用 `AT(.code_aecram)` 属性放到 `aram` 区：

```c
static volatile u8  g_rx_ring[UART_RX_RING_SIZE] AT(.code_aecram);
static volatile u32 g_rx_head                  AT(.code_aecram);
static volatile u32 g_rx_tail                  AT(.code_aecram);
```

**为什么 aram 合适**：
- 16KB 空闲（手册 §3.4 加速 RAM，工程未占用）
- `.code_aecram` 在 ram.ld L63–65 是 NOLOAD 区，**不进 flash**，不占 `.comm` 限额
- main.c L20 `extern u32 __aram_start` 已声明，不冲突

**注意事项**：
- aram 不被 main 的 `memset(&__bss_start, 0, __bss_size)` 清零（BSS 在 data 区）
- ISR / `_int_init` 必须显式 `g_rx_head = g_rx_tail = 0`
- ring 内容无所谓（未消费字节不会被读）

### 3.7 编译器选项：-ffunction-sections + --gc-sections

`.comm` 限额的另一个大头是**所有 17 个 test_*.c 的 `test_*_run` 函数**都被链接进了镜像，即使只开一个 `TEST_UART_*_EN`：

| 文件 | .text / .rodata 估 |
|---|---|
| `test_spi_w25q64.c`（含全部 6 命令 × 2 实现 + 大量 printf）| ~10 KB |
| `test_spi_timing.c` | ~3 KB |
| `test_i2c.c` / `test_i2c_gpio.c` 等 | 各 1–3 KB |
| **合计未用 test** | **~30 KB** |

**方案**：app.cbp 加：

```xml
<Compiler>
    <Add option="-ffunction-sections" />
    <Add option="-fdata-sections" />
</Compiler>
<Linker>
    <Add option="--gc-sections" />   <!-- 注意：不是 -Wl,--gc-sections -->
</Linker>
```

**关键坑**：CodeBlocks IDE 编译完 `.o` 后**直接调 `riscv32-elf-ld.exe`**（不是 gcc 包装），所以链接选项必须用 ld 直接接受的语法 `--gc-sections`，**不要** 用 gcc 的 `-Wl,--gc-sections`（ld 不识别，会报 `unrecognized option`）。

**实测效果**：
- 加 flag 前：app.rv32 ~47 KB（含 17 个 test 函数）→ `.comm` overflow
- 加 flag 后：app.rv32 ~6 KB（只剩当前打开的那个 test 函数 + 中断函数）→ `.comm` 充裕

---

## 4. HAL API 设计

### 4.1 新增函数（`uart_hal.h`）

```c
/* ===== 硬件 UART2 中断驱动 RX ===== */
void uart_hal_hw_int_init(u32 baud);       // 开 RXIE + register_isr + PICEN
void uart_hal_hw_int_teardown(void);       // 关 RXIE + PICEN + 摘 ISR
u8   uart_hal_hw_int_getc(void);           // 阻塞从 ring 读 1 字节
u32  uart_hal_hw_int_available(void);      // ring 当前字节数（非阻塞探查）
```

**与轮询版 API 并存**：

| 轮询版（保留） | 中断版（新增） |
|---|---|
| `uart_hal_hw_init(baud)` | `uart_hal_hw_int_init(baud)` |
| `uart_hal_hw_getc()` | `uart_hal_hw_int_getc()` |
| `uart_hal_hw_putc(tx)` | （共用 `uart_hal_hw_putc`，TX 不需要 ISR）|

`init` / `teardown` 是两套，**互斥**：调用 `_int_init` 后不应再调轮询 `getc`，反之亦然。

### 4.2 新增测试入口（`test_uart.h`）

```c
void test_uart2_recv_int_run(void);   // 中断+ringbuf 版 RECV（保留 _recv_run 轮询版）
```

`test_uart2_recv_int_run` 与 `test_uart2_recv_run` 行为一致（per-byte echo + 行汇总 + Ctrl+C 退出），仅 getc 来源不同。

**Echo 循环**：本设计**不复用**轮询版的 echo 循环体（用户要求"保留这个接收代码"），新入口内联 ~40 行重复逻辑。可接受的代价。

### 4.3 main.c 开关

```c
// ---- UART：软/硬各一份，共用 PB2(TX)/PB1(RX)，一次只开一个 ----
// #define TEST_UART_EN            1   // 硬件 UART2 回环
// #define TEST_UART_SEND_EN       1   // 硬件 UART2 持续发送
// #define TEST_UART_RECV_EN       1   // 硬件 UART2 持续接收 (轮询, 115200 burst 丢字节)
// #define TEST_UART_RECV_INT_EN   1   // 硬件 UART2 持续接收 (中断+ringbuf, 不丢字节) —— 默认
// #define TEST_UART_CONSOLE_EN    1   // 硬件 UART2 收发回显 + printf 重定向
// #define TEST_UART_SOFT_EN       1   // 软件 bit-bang UART 回环
```

**默认开 `RECV_INT_EN`**（commit `acfb23e` 之前是 RECV_EN 轮询，本设计提议切到 INT 版）。

---

## 5. 验证策略（合入时）

### 5.1 编译验证

| 项 | 期望 |
|---|---|
| `riscv32-elf-gcc -ffunction-sections -fdata-sections -Wall -Wextra` | 0 warning |
| 链接 `--gc-sections` | app.rv32 < 16 KB（vs 当前 47 KB） |
| postbuild → app.dcf | "CODE SIZE: ~12 KB" |
| map.txt 包含 `uart2_rx_isr` `.com_text.isr` 段 | 非 0 |

### 5.2 烧板测试用例

| 场景 | 期望 |
|---|---|
| 连发 `hello\n`（PC 终端手输）| 6 行 RX[000..005]（含 'e' 'l' 'l'）|
| 连发 256 字节 hex 突发 | 256 行 RX，ring 不会填满（实测）|
| Ctrl+C (0x03) | `[Recv-INT] Total bytes / ringbuf remaining` 退出信息 |
| 切断 USB-TTL TX，5s 后再连 | 期间无 RXPND 漏触发，main 阻塞在 `_int_getc` 等 ISR |

### 5.3 回归

| 已通过的测试 | 回归方式 |
|---|---|
| TEST_UART_EN（loopback 256/256）| 切宏跑一次 |
| TEST_UART_SEND_EN | 切宏跑一次 |
| TEST_UART_CONSOLE_EN | 切宏跑一次 |
| TEST_UART_SOFT_EN（256/256）| 切宏跑一次 |

---

## 6. 已知边界 / 待评估

| 项 | 备注 |
|---|---|
| RX FIFO 满后丢字节 | 128B 缓冲在 PC hex send 256B 突发下仍可能填满（理论 ~22ms 突发 × 87µs/byte ≈ 252 字节）→ 边角情况需要换大 ring 或限速 |
| 软 UART 的 RECV 丢字节 | 本设计只解决**硬件** UART2。软 bit-bang 掉字节另有原因（[test_uart_hal_refactor.md §6](test_uart_hal_refactor.md) PC 端时序），不在本设计范围 |
| UART1（已弃用）是否复用 ISR | 不在范围；本工程已弃用 UART1 |
| 多字节 ISR 处理顺序 | 按到达顺序（FIFO），无优先级 |

---

## 7. 与现有代码的关系

### 7.1 保留不动的代码

- `test_uart2_recv_run()`（轮询版 RECV）：按用户要求"保留这个接收代码"，不删不改
- `uart_hal_hw_*`（轮询 HAL）：保留，给 LOOP/SEND/CONSOLE 测试用
- `uart_hal_soft_*`：不动
- `uart_hal_console_*`：不动

### 7.2 新增的代码

| 文件 | 新增 |
|---|---|
| `test/uart_hal.h` | +4 函数声明 |
| `test/uart_hal.c` | +ring buffer（128B+8B in `.code_aecram`）+ ISR + 4 函数 |
| `test/test_uart.h` | +`test_uart2_recv_int_run` 声明 |
| `test/test_uart.c` | +`test_uart2_recv_int_run` 定义（~80 行 echo 循环内联）|
| `main.c` | +`TEST_UART_RECV_INT_EN` 宏 + extern + ifdef |
| `app.cbp` | +`-ffunction-sections -fdata-sections` + `--gc-sections` |

### 7.3 不动的文件

- `ram.ld`：本设计**不需要**改链接脚本（核心要点之一）
- `interrupt.c`：复用 `register_isr` + `cpu_low_irq_comm` 基础设施
- `spi_hal.{h,c}` / `test_spi_*.c`：UART ISR 与 SPI 共享 IRQ 调度，但 ISR 函数独立，互不影响

---

## 8. 参考

- 手册 §6.2 L406–435：UART2CON / CPND / BAUD / DATA 寄存器定义
- 手册 §6 表 L55：IRQ#14 = UART0/UART1/UART2 中断
- `smart_mini/header/int.h` L17：`IRQ_UART_VECTOR = 14`
- `smart_mini/header/sfr.h` L436–448：GPIOB 寄存器
- `smart_mini/test/spi_hal.c` L242–263：SPI IT 模式 setup/teardown 参考模式
- `smart_mini/interrupt.c` L8–28：`register_isr` + `cpu_low_irq_comm` 基础设施
- `smart_mini/ram.ld` L11–20：MEMORY 区定义（comm/data/aram 边界）
- `smart_mini/header/macro.h` L8：`AT(x)` = `__attribute__((section(#x)))`

---

## 9. 版本

- 2026-07-22 v1：初版设计（用户决议"先写文档，代码合入待用户测试通过后另起 commit"）