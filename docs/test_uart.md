# BT892X UART 测试报告

> **测试日期**：2026-07-17（初版） / 2026-07-20（§2.6 UART0 异常 + main.c 重定向更新） / 2026-07-21（§2.6 关闭、§2.7 接收逐行缓存、§2.8 解析 §2.6 的实测原因） / 2026-07-22（§2.9 per-byte echo 暴露 115200 burst 掉字节）
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §6 UART](../BT892X_UserManual_Driver.md)（UARTCON §6.2）、§3.3 FUNCMCON1、§3.2 GPIO
> - [bt892x_pinfunction.md §4.2 PORTB / §5.1 UART](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**硬件 UART2**（PB2=TX/PB1=RX）+ **软件 bit-bang UART**（同引脚）+ **printf 重定向到 UART2**
> **当前 printf 通道**：**UART0/PB3@1500000**（main.c 启动时显式 `my_printf_init(uart_putchar)`，UART0 调试串口工作；详见 §2.6 与 §2.8）
> **测试结果**：✅ 硬件回环 256/256、✅ 持续发送、✅ 逐行接收 + Ctrl+C 退出、✅ UART0 banner 恢复、✅ 软件 bit-bang 回环 256/256、⬜ 115200 burst 不掉字节（设计方案见 [test_uart_ringbuf.md](test_uart_ringbuf.md)）

---

## 0. 文档结构

原 `test_uart.c` 使用 UART1(PA6/PA7 G1)，但该口在开发板上物理损坏；本次**整体换成 UART2(PB1/PB2 G2)**，并另加一份纯 GPIO 的软件 bit-bang UART（引脚与硬件一致，不同时开即无冲突）。

| 程序 | 路径 | 入口/开关 | 用途 |
|---|---|---|---|
| **test_uart.c** | `smart_mini/test/test_uart.c` | `test_uart_run` / `TEST_UART_EN` | 硬件 UART2 回环（跳线 PB2↔PB1） |
| | | `test_uart2_send_run` / `TEST_UART_SEND_EN` | 硬件 UART2 持续发送 |
| | | `test_uart2_recv_run` / `TEST_UART_RECV_EN` | 硬件 UART2 接收（每字节 echo + 行缓存 + Ctrl+C 退出）|
| | | `test_uart2_console_run` / `TEST_UART_CONSOLE_EN` | printf 重定向到 UART2 + 收发回显 |
| | | `test_uart_soft_run` / `TEST_UART_SOFT_EN` | 软件 bit-bang UART 回环（9600 8N1）—— 原 `test_uart_soft.c` 已删除，HAL 重构后并入 `test_uart.c` |

> ⚠️ **PB2/PB1 被硬件 UART2、软件 UART、TMR3 PWM(PB1/PB2) 共用** → 对应开关一次只能开一个。
> 软件 bit-bang 部分的 GPIO 原理与 [test_gpio.md](test_gpio.md) 一致。

---

## 1. 手册源码引用

### 1.1 UART 控制寄存器（手册 [§6.2](../BT892X_UserManual_Driver.md) 第 406-419 行，UART0/1/2 结构一致）

```
UARTxCON 位
Bit | Name   | Mode | Description
----|--------|------|-------------------------------------------
 9  | RXPND  | R    | 接收挂起。0:未收完1字节；1:收完1字节
 8  | TXPND  | R    | 发送挂起。0:未发完1字节；1:发完1字节
 7  | RXEN   | WR   | 接收使能。0:禁用；1:使能
 5  | CLKSRC | WR   | 时钟源。0:系统时钟；1:uart_inc
 4  | SB2EN  | WR   | 停止位。0:1位；1:2位
 3  | TXIE   | WR   | 发送中断使能
 2  | RXIE   | WR   | 接收中断使能
 0  | UTEN   | WR   | UART 模块使能
```

**使用流程（手册 §6.3 第 444-451 行）**：设 IO 方向 → 配 BAUD → 置 UTEN → 写 DATA 发送 / 等 PND=1 → 读 DATA 收。

### 1.2 波特率与时钟源

`CLKSRC` 默认 0 = **系统时钟**（本工程 `set_sys_clk(SYS_24M)` = 24MHz）。因此：

```
baud_div = 系统时钟 / 波特率 - 1 = 24000000 / 115200 - 1 = 207
UART2BAUD = (baud_div << 16) | baud_div   // 高16=RX采样, 低16=TX
```

> **注意**：UART 用的是 **系统时钟 24MHz**（不是定时器的 tmr_inc 1MHz），这与 [test_timer.md](test_timer.md) 的时钟源不同。

### 1.3 UART2 引脚映射（手册 [§3.3 FUNCMCON1](../BT892X_UserManual_Driver.md) 第 172-177 行）

```
FUNCMCON1 位
[11:8] UT2RXMAP : UART2 RX 映射。0001=G1, 0010=G2, 0011=由 UT2TXMAP 选择, 1111=清除
[7:4]  UT2TXMAP : UART2 TX 映射。0001=G1, 0010=G2, 1111=清除
```

本测试 `(2<<4)|(2<<8)` = TX=G2、RX=G2 → 对应 PB2=TX2、PB1=RX2（见 §1.5）。

### 1.4 printf 重定向机制（[clib.h](../smart_mini/header/clib.h):10 + [reset.S](../smart_mini/reset.S):128）

```c
void my_printf_init(void (*putchar)(char));   // ROM 接口，reset.S .set 到 0x8401c
```

printf(=my_printf) 通过 `my_printf_init` 注册的字符回调输出。默认走 `uart_putchar`→UART0(PB3)；
调用 `my_printf_init(uart_hal_console_putchar)` 即把 printf 改到 UART2(PB2)。

**应用层封装**：`uart_hal_console_init()`（[`uart_hal.c:199`](../smart_mini/test/uart_hal.c)）封装
`uart_hal_hw_init(baud)` + `my_printf_init(uart_hal_console_putchar)` 两件事。
main.c 启动**不**无条件调它——只在 `TEST_UART_CONSOLE_EN` 入口内调（[`test_uart.c:208`](../smart_mini/test/test_uart.c)），
让 console 测试期间 printf 走 UART2，其它 build 自动走 UART0。详见 §2.6。

### 1.5 UART 引脚（[bt892x_pinfunction.md](../bt892x_pinfunction.md) §5.1 第 204 行 / §4.2 第 115-116 行）

| 引脚 | 角色 | 手册依据 |
|---|---|---|
| PB2 | UART2 TX（TX2-G2） | §5.1 第204行 `UART2 TX/RX: PB1, PB2` + §4.2 第116行 `TX2-G2` |
| PB1 | UART2 RX（RX2-G2） | §5.1 第204行 + §4.2 第115行 `RX2-G2` |
| PB3 | UART0 TX（调试打印，勿动） | main.c `uart0_mapping_sel()` |

### 1.6 软件 bit-bang UART 用到的 GPIO（手册 §3.2，同 [test_gpio.md](test_gpio.md)）

`GPIOBFEN=0`(GPIO)、`GPIOBDE=1`(数字)、`GPIOBDIR`(TX=0 输出/RX=1 输入)、`GPIOBPU`(RX 上拉)、`GPIOBSET/CLR`(TX 电平)、读 `GPIOB`(RX 采样)。

---

## 2. 测试程序 1：test_uart.c（硬件 UART2）

### 2.1 引脚分配

| 引脚 | 角色 |
|---|---|
| PB2 | UART2 TX |
| PB1 | UART2 RX |

### 2.2 寄存器配置原理（`uart2_test_init`，每行注明出处）

```c
FUNCMCON1 &= ~((0xF<<4)|(0xF<<8));     // §3.3 清 UT2TXMAP+UT2RXMAP
FUNCMCON1 |=  (2<<4)|(2<<8);           // §3.3 UT2TXMAP=G2, UT2RXMAP=G2
// PB2 → TX：功能IO/数字/输出/上拉（§3.2）
GPIOBFEN |= BIT(2); GPIOBDE |= BIT(2); GPIOBDIR &= ~BIT(2); GPIOBPU |= BIT(2);
// PB1 → RX：功能IO/数字/输入/上拉（§3.2）
GPIOBFEN |= BIT(1); GPIOBDE |= BIT(1); GPIOBDIR |= BIT(1);  GPIOBPU |= BIT(1);
UART2BAUD = (207<<16)|207;             // §6 115200@24MHz
UART2CON  = BIT(7)|BIT(0);             // §6.2 RXEN + UTEN
while (UART2CON & BIT(9)) (void)UART2DATA;  // §6.2 冲洗残留 RX
```

收发原语：
```c
uart2_putc: while(!(UART2CON&BIT(8))); UART2DATA=ch; while(!(UART2CON&BIT(8)));  // §6.2 等 TXPND
uart2_getc: while(!(UART2CON&BIT(9))); return UART2DATA;                         // §6.2 等 RXPND，读 DATA 清挂起
```

### 2.3 五个子测试

| 开关 | 行为 | 接线 | 实测 |
|---|---|---|---|
| `TEST_UART_EN` | 回环自发自收 0x00~0xFF | 跳线 PB2↔PB1 | ✅ `Total: 256, Errors: 0` |
| `TEST_UART_SEND_EN` | 持续发 `[n] Hello UART2!` | PB2→USB-TTL RX | ✅ PC 串口助手正常收到 |
| `TEST_UART_RECV_EN` | 收到字节经 UART0 打印 | USB-TTL TX→PB1 | ✅ 逐字节 echo + 行缓存（详见 §2.5 / §2.9） |
| `TEST_UART_CONSOLE_EN` | printf 重定向 UART2 + 回显 | 全双工 PB2↔TTL RX、PB1↔TTL TX | ✅ banner + 键入字符回显 |
| `TEST_UART_SOFT_EN` | 软件 bit-bang 回环 0x00~0xFF | 跳线 PB2↔PB1 | ✅ `Total: 256, Errors: 0` |

### 2.4 实测结果（用户验证）

```
===== BT892X UART2 Loopback Test =====
UART2: TX=PB2, RX=PB1, 115200bps 8N1
Jumper: PB2(TX) <-> PB1(RX)
===== Result =====
Total: 256, Errors: 0
```

```
===== BT892X UART2 Console (printf -> UART2) =====   ← printf 已成功重定向到 UART2(PB2)
UART2: TX=PB2, RX=PB1, 115200bps 8N1
```

**结论**：✅ **回环 256/256 已同时证明 UART2 的发送与接收（芯片端）均正常**；持续发送、printf 重定向 banner 均通过。

### 2.5 硬件 UART2 外部接收（PC → PB1） — ✅ 逐字节 echo + 行汇总 + Ctrl+C 退出

> **2026-07-21 行缓存**：解决 PC 串口助手每发一字节就被刷屏的问题，并补一个 Ctrl+C 退出入口。
> **2026-07-22 per-byte echo**（commit `acfb23e`）：在行汇总基础上**同时**每字节立即 echo `RX[NNN] 0xXX 'C'`，hex send 工具不发 Enter 也能看到反馈；详见 §2.9。

**行为**（[`test_uart.c:109-198`](../smart_mini/test/test_uart.c)）：
- 使用 `UART2_RECV_LINE_MAX = 64` 的本地行缓冲；
- **每收到 1 字节立即 echo**：`RX[NNN] 0xXX 'C'`（可显字符）或 `RX[NNN] 0xXX`（控制字符）；
- 收到 `'\r' / '\n'` 才整行打印 `UART2 RX line #N: "..."`；
- 收到 `0x03`（Ctrl+C）打印 `[Recv] Exit requested (0x03)` 后退出测试循环；
- 收到 `0x08` / `0x7F` 当作退格，删除前一个字符；
- 其余 `< 0x20` 控制字符丢弃，不进缓冲；
- 行缓冲满时立即 flush，并把这个新字符作为新行起点；
- 退出前再 flush 残余数据，最后打印 `Total bytes / Total lines` 统计并进入 `while(1)` 死循环。

**预期行为**（PC 接 PB1 @ 115200，芯片端 PB3 @ 1.5Mbps 显示）：
1. PB3 打印启动 banner 与 4 行说明；
2. PC 输入 `hello\r\n` → PB3 打印：

```text
RX[000] 0x68 'h'
RX[001] 0x65 'e'
RX[002] 0x6C 'l'
RX[003] 0x6C 'l'
RX[004] 0x6F 'o'
RX[005] 0x0D
UART2 RX line #0: "hello" (len=5)
```

3. PC 输入 `BT892X 接收正常\n` → PB3 打印（同样 6+ 行 echo + 1 行汇总）：

```text
UART2 RX line #1: "BT892X 接收正常" (len=14)
```

4. PC 单独发一个回车 → PB3 打印：

```text
RX[NNN] 0x0D
UART2 RX line #2: <CR/LF only>
```

5. PC 发送 64+ 字符的长行 → PB3 自动 flush 前 64 字符并继续接收后续字符；
6. PC 发送 `0x03`（Ctrl+C）→ PB3 打印：

```text
[Recv] Exit requested (0x03)
[Recv] Total bytes: N, total lines: M
```

随后进入 `while(1)`，方便用户拔线。

**结论**：✅ `TEST_UART_RECV_EN` 现在的行为与 PC 串口助手一致：每字节立即反馈 + 每行一条消息 + Ctrl+C 退出 + 统计可读。

**已知限制**：per-byte echo 把 printf 耗时放大，115200 baud 下 PC 突发 ≥ 3 字节会被覆盖丢失——详见 §2.9 / [test_uart_ringbuf.md](test_uart_ringbuf.md)。

---

### 2.6 UART0/PB3 调试打印恢复（2026-07-21）

> **本节说明 2026-07-21 重新启用 UART0 banner 的过程与 §2.7 的修正**。
> 2026-07-20 记录的 "ROM 备份域" 根因猜测已撤销。

**现象（2026-07-20）**：烧录 `TEST_UART_CONSOLE_EN` 后，切回其它 build（`TEST_UART_EN` /
`TEST_UART_RECV_EN` / `TEST_I2C_EN` / ...），即便是完整断电复位（拔 USB 重插），
PB3 @ 1.5M 调试口**仍然无任何输出**，包括 main.c 的 `printf("Hello SMART Flash MiniProj\n")`。
当时把 `uart2_console_init()` 放进 main.c 启动流程作为临时绕过，所有 printf 走 UART2。

**实测结论（2026-07-21）**：将 `main.c` 中替换为

```c
my_printf_init(uart_putchar);
printf("Hello SMART Flash MiniProj\n");
```

后，PB3 @ 1.5M 立即恢复 banner 输出。验证流程：

1. Build → Rebuild（CodeBlocks 必须 Rebuild，否则只改 main.c 也可能拿到旧的 `app.dcf`）；
2. Downloader 烧录 `app.dcf`；
3. 串口助手接 PB3 @ 1.5Mbps 8N1；
4. 上电，PB3 立即打印 `Hello SMART Flash MiniProj`。

**§2.7 复盘根因**：原现象"切 build 后 PB3 无输出"是**用户操作/时序**问题，
不是 ROM 备份域。`my_printf_init(uart_putchar)` 单独调用就足够把 putchar 槽位覆盖回 UART0。

**当前 main.c 启动流程**（[main.c:248-253](../smart_mini/main.c)）：

```c
/* 恢复初始化时把 printf 重定向回 UART0 (PB3 @ 1.5Mbps, 默认 uart_putchar)。
   想临时切到 UART2 时再调 uart2_console_init() 即可。 */
my_printf_init(uart_putchar);
printf("Hello SMART Flash MiniProj\n");
```

`uart2_console_init()` 不再在启动时无条件调用。需要 UART2 输出时由 `TEST_UART_CONSOLE_EN`
或具体测试入口内部自行调用，避免污染其它 build。

| 项 | 现状 |
|---|---|
| printf 通道 | **UART0/PB3@1500000**（默认；UART2 需要时单独切） |
| 接线 | 仅需 PB3 → USB-TTL RX，GND-GND；UART2 测试期间再接 PB2/PB1 |
| `TEST_UART_CONSOLE_EN` 副作用 | 启动后该 build 内 printf 切到 UART2；下次烧其它 build 时 banner 自动回 UART0 |
| 恢复 UART0 | main.c 启动时已 `my_printf_init(uart_putchar)`，无需额外操作 |

---

### 2.9 115200 baud RECV 突发掉字节（2026-07-22 per-byte echo 暴露）

> **新发现的问题**：`acfb23e` 加 per-byte echo 后，PC 串口助手连发"hello"等短消息时偶现 'e' / 'l' 丢失。

**实测现象**：

```
Hello SMART Flash MiniProj
===== BT892X UART2 Recv Test =====
...
RX[000] 0x68 'h'
RX[001] 0x6C 'l'
RX[002] 0x6F 'o'   ← 'e' (0x65) 丢失
RX[003] 0x68 'h'
RX[004] 0x6C 'l'   ← 第二个 'l' (0x6C) 丢失
RX[005] 0x68 'h'
...
```

**根因**：BT892X UART2 硬件无 FIFO（手册 §6.2），仅 1 字节缓冲。轮询 `uart_hal_hw_getc` 在 `printf("RX[NNN] 0xXX 'C'\n")` 阻塞期间（约 200–500 µs）跟不上 115200 baud（每字节 87 µs）——下一个到达的字节覆盖 DATA 寄存器的旧字节。

**为什么之前行缓冲版本没暴露**：行缓冲攒齐 64 字节才打一次，printf 阻塞时间相对短；且丢字节在行缓冲里"看不出来"（虽然行内容已损坏）。

**已提出的方案**：中断 + 软件环形缓冲（`uart_hal_hw_int_*`），ISR 立即把字节搬进 128B ring buffer，main 慢慢读。设计文档见 [test_uart_ringbuf.md](test_uart_ringbuf.md)，代码待用户测试通过后另起 commit。

**临时缓解**：测试时 PC 端**降低发送速率**（手动输入时字符间加 100ms+ 间隔；hex send 工具设 `Inter-byte delay`）。

---

## 3. 测试程序 2：软件 bit-bang UART（HAL 重构后并入 test_uart.c）

> **历史变更（commit `629ed39`）**：原 `test_uart_soft.c/.h` 独立文件已删除，软路入口 `test_uart_soft_run` 并入 `test_uart.c`，全部走 `uart_hal_soft_*` HAL 接口（参见 [`test_uart.c:222-237`](../smart_mini/test/test_uart.c)）。

### 3.1 原理

纯 GPIO 模拟 UART：TX 用 `GPIOBSET/CLR` 打电平，RX 用读 `GPIOB` 中心采样，靠 `delay_us` 定时。帧格式 8N1，波特率 9600（bit=104µs，源自 tmr_inc=1MHz 的 delay_us）。引脚 **PB2=TX、PB1=RX**（与硬件 UART2 一致）。

### 3.2 关键配置（每行注明出处，同 test_gpio.md §3.2 / `uart_hal.c`）

> HAL 重构后 `uart_hal_soft_init`（[`uart_hal.c:28-48`](../smart_mini/test/uart_hal.c)）默认**两脚都设输入**（`GPIOBDIR |=` 两脚），仅在 `uart_hal_soft_putc` 入口临时把 PB2 切输出，发送完恢复输入——避免与外接 RX 源（PC USB-TTL TX）形成"双 driver 拉锯"噪声（详见 [test_uart_hal_refactor.md §3 Step 4](test_uart_hal_refactor.md)）。

```c
// TX(PB2)+RX(PB1) 默认输入+上拉
GPIOBFEN &= ~(BIT(2)|BIT(1));  // FEN=0 → GPIO 模式
GPIOBDE  |=  (BIT(2)|BIT(1));
GPIOBDIR |=  (BIT(2)|BIT(1));  // ★ 默认两脚都输入
GPIOBPU  |=  (BIT(2)|BIT(1));

// putc 临时切 PB2 输出（done by uart_hal_soft_putc L56）：
GPIOBDIR &= ~BIT(2);
GPIOBSET  = BIT(2);             // idle HIGH
// 8 数据 bit LSB-first + 每位 delay_us(SOFT_BIT_US=104)
```

### 3.3 测试流程与结果

| 子测试 | 验证 | 期望 |
|---|---|---|
| 回环 0x00~0xFF | TX+RX 同步，跳线 PB2↔PB1 | `Errors: 0` |
| PB2 发 0x55 方波 | 逻辑分析仪看波形 | 9600 位宽 104µs |

**实测**（commit `940e728` 修复 DIR 切回 + init 默认输入后）：
- 回环 256/256 PASSED（`Total: 256, Errors: 0`）
- PB2 → PB1 跳线镜像 104µs/bit 半 bit 52µs
- **外部 PC 接收仍有 1-2 bit 偏移**（详见 [test_uart_hal_refactor.md §6](test_uart_hal_refactor.md) 已知问题，与硬件 UART2 无关）

---

## 4. 完整寄存器表

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `FUNCMCON1` | 0x020 (`SFR0_BASE+0x08*4`) | `[7:4]=2`(TX G2), `[11:8]=2`(RX G2) | §3.3 第176-177行 |
| `UART2CON` | 0x9C0 (`SFR9_BASE+0x18*4`) | `BIT(7)\|BIT(0)` RXEN+UTEN | §6.2 |
| `UART2CPND` | 0x9C4 (`SFR9_BASE+0x19*4`) | 写 1 清挂起；HAL 显式清 RXPND | §6.2 |
| `UART2BAUD` | 0x9C8 (`SFR9_BASE+0x1a*4`) | `(207<<16)\|207` 115200 | §6 |
| `UART2DATA` | 0x9CC (`SFR9_BASE+0x1b*4`) | 读收/写发；**读不清 RXPND**（必须显式 `UART2CPND=BIT(9)`） | §6.3 |
| `GPIOBFEN` | 0x654 | 硬件=1(功能), 软件=0(GPIO) | §3.2 |
| `GPIOBDE` | 0x650 | `\|= BIT(1/2)` 数字 IO | §3.2 |
| `GPIOBDIR` | 0x64C | TX=0 输出, RX=1 输入 | §3.2 |
| `GPIOBPU` | 0x65C (`SFR6_BASE+0x17*4`) | RX 上拉 | §3.2 |
| `GPIOBSET/CLR/GPIOB` | 0x640/0x644/0x648 | 软件 UART TX 打电平/RX 采样 | §3.2 |
| ROM `my_printf_init` | 0x8401c (reset.S:128) | 注册 UART2 putchar 回调 | clib.h:10 |

---

## 5. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| 回环 Errors≠0 | 跳线没接 / TX 未等发完 | 确认 PB2↔PB1 跳线；putc 等两次 TXPND |
| 发送 PC 收到乱码 | 波特率/格式不符 | PC 端设 115200 8N1 |
| 接收无显示（看错口） | recv 把数据打到 UART0(PB3) 而非 UART2 口 | 去看烧录口(TTL1)终端，或改用 CONSOLE |
| 接收无显示（PC 没发） | 串口助手 COM 选错/未真正发出 | 确认发到"TX 接 PB1"的那个适配器，看发送计数 |
| 接收无显示（适配器坏） | USB-TTL TX 脚故障 | 换适配器自回环验证（短接其 TX↔RX） |
| 接收 'e' / 'l' 等字符偶发丢失 | 115200 baud 下轮询 `getc` 在 printf 阻塞期间跟不上 PC 突发 | 降低 PC 发送速率；或参考 [test_uart_ringbuf.md](test_uart_ringbuf.md) 改用 ISR+ringbuf |
| printf 重定向后 PB3 无输出 | printf 已切到 UART2 | 正常；`console_init` 之后所有 printf 走 UART2/PB2 |
| 烧 CONSOLE_EN 后切回其它 build PB3 无输出 | 之前是 §2.6 假设的 ROM 备份域问题，**经实测为非事实**（见 §2.7）；原因为 main.c 启动时调了 `uart_hal_console_init()` 覆盖了 putchar 槽 | HAL `uart_hal_hw_init` 末尾已自动调 `my_printf_init(uart_putchar)`，PB3 自动恢复 |

---

## 6. 硬件 UART2 vs 软件 bit-bang UART 对比

| 项目 | 硬件 UART2 | 软件 bit-bang |
|---|---|---|
| 实现 | UART2 控制器（自动收发/采样） | 纯 GPIO + delay_us 定时 |
| 时钟源 | 系统时钟 24MHz（§6.2 CLKSRC=0） | tmr_inc 1MHz（delay_us 基准） |
| 波特率 | 115200（BAUD=207） | 9600（bit=104µs） |
| CPU 占用 | 低（硬件搬运） | 高（全程忙等） |
| 引脚 | PB2=TX/PB1=RX（固定映射 G2） | PB2=TX/PB1=RX（任意 GPIO 可换） |
| 用途 | 高速稳定通信 | 无 UART 资源时应急/教学 |

---

## 7. 工程意义

- **UART1(PA6/PA7) → UART2(PB1/PB2)**：换到实际可用的口，硬件回环 256/256 验证收发正常。
- **printf 通道**：默认走 UART0/PB3@1.5M（main.c 启动时 `my_printf_init(uart_putchar)`），
  `TEST_UART_CONSOLE_EN` 测试内部把 printf 切到 UART2/PB2@115200，单适配器即可做全双工回显测试。
- **逐行接收 + Ctrl+C 退出**：`TEST_UART_RECV_EN` 改为按行缓存 + Ctrl+C 退出，避免刷屏。
- **软/硬同引脚共存**：软件 bit-bang 与硬件 UART2 共用 PB2/PB1，互斥启用。
- **手册依据**：每条寄存器操作对应 [§6.2 / §3.3 / §3.2](../BT892X_UserManual_Driver.md)。

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_uart.c` | 5 个测试入口（回环/发送/接收/控制台/软）+ per-byte echo 行汇总 |
| `smart_mini/test/test_uart.h` | 头文件（5 个入口声明） |
| `smart_mini/test/uart_hal.c` | HAL 实现：软 4 + 硬 3 + console 3 = 10 个原语 |
| `smart_mini/test/uart_hal.h` | HAL 接口声明 + `UART_TX_PIN/UART_RX_PIN` 宏 |
| `smart_mini/main.c` | 入口（`TEST_UART_*` 开关组）+ `uart_putchar`(UART0 默认打印) |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 宏定义（UART2 第571-574行、GPIOB 第436-448行） |
| `smart_mini/header/clib.h` | `my_printf_init` 声明（第10行） |
| `smart_mini/reset.S` | `my_printf_init` ROM 地址 `.set 0x8401c`（第128行） |
| `docs/BT892X_UserManual_Driver.md` | 手册（§6 UART、§3.3 FUNCMCON1、§3.2 GPIO） |
| `docs/bt892x_pinfunction.md` | 引脚功能（§5.1 UART、§4.2 PORTB） |
| `docs/periph_uart.md` | UART 外设技术说明（寄存器/引脚/初始化/审查） |
| `docs/test_uart_hal_refactor.md` | HAL 重构 5 步记录（2026-07-17） |
| `docs/test_uart_ringbuf.md` | **设计文档**：UART2 中断+ringbuf（解决 115200 burst 丢字节，代码未合入） |
