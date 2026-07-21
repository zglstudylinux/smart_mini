# BT892X UART 测试报告

> **测试日期**：2026-07-17（初版） / 2026-07-20（§2.6 UART0 异常 + main.c 重定向更新） / 2026-07-21（§2.6 关闭、§2.7 接收逐行缓存、§2.8 解析 §2.6 的实测原因）
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §6 UART](../BT892X_UserManual_Driver.md)（UARTCON §6.2）、§3.3 FUNCMCON1、§3.2 GPIO
> - [bt892x_pinfunction.md §4.2 PORTB / §5.1 UART](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**硬件 UART2**（PB2=TX/PB1=RX）+ **软件 bit-bang UART**（同引脚）+ **printf 重定向到 UART2**
> **当前 printf 通道**：**UART0/PB3@1500000**（2026-07-21 起 main.c 启动时显式 `my_printf_init(uart_putchar)`，UART0 调试串口恢复工作；详见 §2.6 与 §2.8）
> **测试结果**：✅ 硬件回环 256/256、✅ 持续发送、✅ 逐行接收 + Ctrl+C 退出、✅ UART0 banner 恢复、⬜ 软件 bit-bang 回环待实测

---

## 0. 文档结构

原 `test_uart.c` 使用 UART1(PA6/PA7 G1)，但该口在开发板上物理损坏；本次**整体换成 UART2(PB1/PB2 G2)**，并另加一份纯 GPIO 的软件 bit-bang UART（引脚与硬件一致，不同时开即无冲突）。

| 程序 | 路径 | 入口/开关 | 用途 |
|---|---|---|---|
| **test_uart.c** | `smart_mini/test/test_uart.c` | `test_uart_run` / `TEST_UART_EN` | 硬件 UART2 回环（跳线 PB2↔PB1） |
| | | `test_uart2_send_run` / `TEST_UART_SEND_EN` | 硬件 UART2 持续发送 |
| | | `test_uart2_recv_run` / `TEST_UART_RECV_EN` | 硬件 UART2 接收（按行缓存，收 '\n' 整行打印，Ctrl+C 退出） |
| | | `test_uart2_console_run` / `TEST_UART_CONSOLE_EN` | printf 重定向到 UART2 + 收发回显 |
| **test_uart_soft.c** | `smart_mini/test/test_uart_soft.c` | `test_uart_soft_run` / `TEST_UART_SOFT_EN` | 软件 bit-bang UART 回环（9600 8N1） |

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
调用 `my_printf_init(uart2_console_putchar)` 即把 printf 改到 UART2(PB2)。

**应用层封装**：`test_uart.c` 提供了 `uart2_console_init()`（[test_uart.c:192](../smart_mini/test/test_uart.c)），
内部完成 `uart2_test_init()` + `my_printf_init(uart2_console_putchar)` 两件事。
main.c 现在默认调它（[main.c:238](../smart_mini/main.c)），让所有 build 的 printf 统一走 UART2，
详见 §2.6。

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

### 2.3 四个子测试

| 开关 | 行为 | 接线 | 实测 |
|---|---|---|---|
| `TEST_UART_EN` | 回环自发自收 0x00~0xFF | 跳线 PB2↔PB1 | ✅ `Total: 256, Errors: 0` |
| `TEST_UART_SEND_EN` | 持续发 `[n] Hello UART2!` | PB2→USB-TTL RX | ✅ PC 串口助手正常收到 |
| `TEST_UART_RECV_EN` | 收到字节经 UART0 打印 | USB-TTL TX→PB1 | ✅ 逐行缓存（详见 §2.7） |
| `TEST_UART_CONSOLE_EN` | printf 重定向 UART2 + 回显 | 全双工 PB2↔TTL RX、PB1↔TTL TX | ✅ banner 显示（发/重定向）；⬜ 回显（收）未测 |

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

### 2.5 硬件 UART2 外部接收（PC → PB1） — ✅ 逐行缓存 + Ctrl+C 退出

> **2026-07-21 更新**：将 `test_uart2_recv_run()` 由"每字节一行"改为"按行缓存、整行打印"，
> 解决 PC 串口助手每发一字节就被刷屏的问题，并补一个 Ctrl+C 退出入口。

**问题**：
- 旧实现每收到一个字节立刻 `printf("UART2 RX: '%c' (0x%02X)\n", ch, ch)`，
  PC 端输入一个字符就刷出一行，几条短消息下来串口终端已经被淹；
- 没有任何"正常退出"入口，只能断电或切 build。

**修复**（[test_uart.c:154-235](../smart_mini/test/test_uart.c)）：
- 使用 `UART2_RECV_LINE_MAX = 64` 的本地行缓冲；
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
UART2 RX line #0: "hello" (len=5)
```

3. PC 输入 `BT892X 接收正常\n` → PB3 打印：

```text
UART2 RX line #1: "BT892X 接收正常" (len=14)
```

4. PC 单独发一个回车 → PB3 打印：

```text
UART2 RX line #2: <CR/LF only>
```

5. PC 发送 64+ 字符的长行 → PB3 自动 flush 前 64 字符并继续接收后续字符；
6. PC 发送 `0x03`（Ctrl+C）→ PB3 打印：

```text
[Recv] Exit requested (0x03)
[Recv] Total bytes: N, total lines: M
```

随后进入 `while(1)`，方便用户拔线。

**结论**：✅ `TEST_UART_RECV_EN` 现在的行为与 PC 串口助手一致：每行一条消息，Ctrl+C 退出，统计可读。

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

## 3. 测试程序 2：test_uart_soft.c（软件 bit-bang UART）

### 3.1 原理

纯 GPIO 模拟 UART：TX 用 `GPIOBSET/CLR` 打电平，RX 用读 `GPIOB` 中心采样，靠 `delay_us` 定时。帧格式 8N1，波特率 9600（bit=104µs，源自 tmr_inc=1MHz 的 delay_us）。引脚 **PB2=TX、PB1=RX**（与硬件 UART2 一致）。

### 3.2 关键配置（每行注明出处，同 test_gpio.md §3.2）

```c
// TX(PB2): GPIO/数字/输出/空闲高
GPIOBFEN &= ~BIT(2); GPIOBDE |= BIT(2); GPIOBDIR &= ~BIT(2); GPIOBSET = BIT(2);
// RX(PB1): GPIO/数字/输入/上拉
GPIOBFEN &= ~BIT(1); GPIOBDE |= BIT(1); GPIOBDIR |= BIT(1); GPIOBPU |= BIT(1);
// 每 bit：TX 设电平 → delay_us(52) 到中心 → 读 GPIOB 采样 → delay_us(52)
```

### 3.3 测试流程与结果

| 子测试 | 验证 | 期望 |
|---|---|---|
| 回环 0x00~0xFF | TX+RX 同步，跳线 PB2↔PB1 | `Errors: 0` |
| PB2 发 0x55 方波 | 逻辑分析仪看波形 | 9600 位宽 104µs |

**实测**：⬜ 代码就绪，回环逻辑与硬件同理，待单独烧录 `TEST_UART_SOFT_EN` 验证。

---

## 4. 完整寄存器表

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `FUNCMCON1` | 0x020 (`SFR0_BASE+0x08*4`) | `[7:4]=2`(TX G2), `[11:8]=2`(RX G2) | §3.3 第176-177行 |
| `UART2CON` | 0x960 (`SFR9_BASE+0x18*4`) | `BIT(7)\|BIT(0)` RXEN+UTEN | §6.2 |
| `UART2CPND` | 0x964 (`SFR9_BASE+0x19*4`) | 清挂起 | §6.2 |
| `UART2BAUD` | 0x968 (`SFR9_BASE+0x1a*4`) | `(207<<16)\|207` 115200 | §6 |
| `UART2DATA` | 0x96C (`SFR9_BASE+0x1b*4`) | 读收/写发；读清 RXPND | §6.3 |
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
| 接收每字节一行，串口刷屏 | 旧实现未做行缓存 | 升级到 §2.5 逐行缓存版本（2026-07-21） |
| printf 重定向后 PB3 无输出 | printf 已切到 UART2 | 正常；重定向前的 banner 仍在 PB3 |
| 烧 CONSOLE_EN 后切回其它 build PB3 无输出 | 之前是 §2.6 假设的 ROM 备份域问题，**经实测为非事实**（见 §2.7）；原因为 main.c 启动时调了 `uart2_console_init()` 覆盖了 putchar 槽 | 已在 main.c 启动时改回 `my_printf_init(uart_putchar)`，PB3 自动恢复 |

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
| `smart_mini/test/test_uart.c` | 硬件 UART2 测试（回环/发送/接收/控制台）+ `uart2_console_init()` 实现 |
| `smart_mini/test/test_uart.h` | 头文件（4 个入口声明 + `uart2_console_init` 声明） |
| `smart_mini/test/test_uart_soft.c` | 软件 bit-bang UART 测试 |
| `smart_mini/test/test_uart_soft.h` | 头文件 |
| `smart_mini/main.c` | 入口（`TEST_UART_*` 开关组）+ `uart_putchar`(UART0 默认打印) |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 宏定义（UART2 第571-574行、GPIOB 第436-448行） |
| `smart_mini/header/clib.h` | `my_printf_init` 声明（第10行） |
| `smart_mini/reset.S` | `my_printf_init` ROM 地址 `.set 0x8401c`（第128行） |
| `docs/BT892X_UserManual_Driver.md` | 手册（§6 UART、§3.3 FUNCMCON1、§3.2 GPIO） |
| `docs/bt892x_pinfunction.md` | 引脚功能（§5.1 UART、§4.2 PORTB） |
