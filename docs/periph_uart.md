# BT892X UART 外设说明文档

> **适用芯片**：中科蓝讯 BT892X（32-bit RISC-V，Audio Player Microcontroller）
> **代码工程**：`smart_mini/smart_mini/app.cbp`（CodeBlocks）
> **本工程定位**：寄存器裸操作，无独立 driver 层；寄存器映射见 `smart_mini/header/sfr.h`
> **本说明文档面向后续维护与教学用途**

---

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 外设概述

BT892X 内部集成 **3 路全双工 UART 控制器**（UART0 / UART1 / UART2，参考 `BT892X_UserManual_Driver.md` §6 概述与 §1.2 主要特性表）。三路结构完全一致，每路含 4 个 SFR：`UARTxCON / UARTxCPND / UARTxBAUD / UARTxDATA`。UART2 还在 SFR Group9 上比 UART0/UART1 多保留一个 `U2KEYCON`（键控/复位键功能），但本工程未使用。

在本工程（`smart_mini`）中 UART 系统由三层组成：

| 层 | 实现 | 路径 | 作用 |
|:---|:---|:---|:---|
| **应用入口** | 5 个 `TEST_UART_*_EN` 宏 | `smart_mini/main.c` L53–60, L282–299 | 一次性选择要跑的测试入口（loop / send / recv / console / soft） |
| **测试层** | `test_uart_run / test_uart2_send_run / test_uart2_recv_run / test_uart2_console_run / test_uart_soft_run` | `smart_mini/test/test_uart.c` | 5 个测试入口，全部走 HAL，不重复实现寄存器层 |
| **HAL 抽象层** | `uart_hal_soft_* / uart_hal_hw_* / uart_hal_console_*` | `smart_mini/test/uart_hal.{h,c}` | 抽出 10 个原语（软 4 + 硬 3 + console 3），所有 init/putc/getc 统一在 HAL |
| **printf 重定向** | `my_printf_init`（ROM 0x8401c）+ `uart_putchar` | `smart_mini/main.c` L94–98, L252；`smart_mini/reset.S` L128；`smart_mini/header/clib.h` L10 | 默认走 UART0/PB3@1.5Mbps 调试串口；可切到 UART2 |

> **重要历史背景**：本工程原本使用 `UART1(PA6/PA7 G1)`，但因该口在开发板上物理损坏，**整体迁移到 `UART2(PB1/PB2 G2)`**。该决定同时体现在 `test_uart.c` 头注释 L10–11 与 `test_uart.md` §0 “整体换成 UART2(PB1/PB2 G2)”。

### 1.2 UART0 / UART1 / UART2 在本工程中的角色

| 通道 | 工程角色 | 引脚 | 波特率 | 备注 |
|:---|:---|:---|:---|:---|
| **UART0** | **默认 printf 通道**（`uart_putchar`） | TX=PB3（G3，调试下载口复用） | 1.5 Mbps（`UART_BAUD=1500000`，`main.c` L17–18, L239） | `main.c` 启动时 `my_printf_init(uart_putchar)` 必显式调用 |
| UART1 | **未使用 / 物理损坏弃用** | PA6/PA7（G1） | — | 已在 `test_uart.c` L10 注释中弃用 |
| **UART2** | 测试 / printf 重定向目标 | TX=PB2 / RX=PB1（G2） | 115200 8N1 | `FUNCMCON1[7:4]=2 / [11:8]=2` |

### 1.3 涉及文件清单（相对路径，相对工程根 `smart_mini/smart_mini/`）

| 相对路径 | 作用 |
|:---|:---|
| `header/sfr.h` | 所有 UART/GP/FUNCMCON 寄存器宏定义（UART0 L54–57、UART1 L88–91、UART2 L571–574、GPIOB L436–448、FUNCMCON0 L44、FUNCMCON1 L45） |
| `header/clib.h` | `my_printf_init` 函数声明（L10） |
| `reset.S` | ROM stub `.set 0x8401c`（L128） |
| `main.c` | 启动、`uart_putchar`（L93–98）、`uart0_mapping_sel`（L167–182）、`set_sys_clk`（L184–222）、`TEST_UART_*_EN` 开关（L53–60）与分发（L282–299） |
| `test/test_uart.c` | 5 个测试入口（loop / send / recv / console / soft）—— 全部走 HAL，不重复实现寄存器 |
| `test/test_uart.h` | 5 个入口声明 |
| `test/uart_hal.c` | HAL 实现：软 4 + 硬 3 + console 3 = 10 个原语 |
| `test/uart_hal.h` | HAL 接口声明 + `UART_TX_PIN=UART_RX_PIN=` `BIT(2)/BIT(1)` |
| `test/test_common.h` | `BIT()` 宏、`delay_us` 等基础工具（被 HAL 依赖） |
| `docs/BT892X_UserManual_Driver.md` | §3.2 GPIO、§3.3 FUNCMCON1、§6 UART 主参考 |
| `docs/bt892x_pinfunction.md` | §4.2 PORTB、§5.1 UART2 引脚表 |
| `docs/test_uart.md` | 实测报告（2026-07-17 / 07-20 / 07-21 三次更新） |
| `docs/test_uart_hal_refactor.md` | HAL 重构 5 步记录（2026-07-17） |
| `docs/test_uart_ringbuf.md` | **设计文档**：UART2 中断 + 环形缓冲（解决 115200 burst 丢字节），代码未合入 |

---

## 2. 涉及寄存器逐个说明

> **地址计算说明**：`SFR0_BASE = 0x000`、`SFR1_BASE = 0x100`、`SFR6_BASE = 0x600`、`SFR9_BASE = 0x900`（见 `sfr.h` L20–35）。`SFR_RW = *(volatile unsigned long *)`，本工程所有 UART SFR 都按 32-bit 字访问。
>
> **手册章节号**：本章所有"§X.Y"引用均来自 `BT892X_UserManual_Driver.md`。

### 2.1 UART2 寄存器（核心测试对象）

| 寄存器 | 地址（基+偏移*4） | sfr.h 行号 | 关键位域 | 含义（对照手册） |
|:---|:---|:---|:---|:---|
| **UART2CON** | `0x900 + 0x18*4 = 0x9C0` | L571 | bit 9 RXPND（R） / bit 8 TXPND（R） / bit 7 RXEN（WR） / bit 5 CLKSRC（WR） / bit 4 SB2EN（WR） / bit 3 TXIE / bit 2 RXIE / bit 1 BIT9EN / bit 0 UTEN（WR） | §6.2 L406–419：RXPND=1 表示收完 1 字节；TXPND=1 表示发完 1 字节；RXEN/UTEN=1 同时打开才能收发 |
| **UART2CPND** | `0x900 + 0x19*4 = 0x9C4` | L572 | bit 9 CRXPND / bit 8 CTXPND（写 1 清） | §6.2 L421–428：清挂起寄存器；写 UART2DATA **也会**自动清 TXPND，但 **不会** 自动清 RXPND——必须显式 `UART2CPND=BIT(9)`（HAL 已修，原始 `uart2_getc` 未显式清，详见 §8） |
| **UART2BAUD** | `0x900 + 0x1a*4 = 0x9C8` | L573 | [31:16] RXBAUD / [15:0] TXBAUD（均 W） | §6.2 L430–435：波特率 `= Fsys / (BAUD + 1)`；同一寄存器里 TX/RX 独立分频，但工程里两者都填同一值 |
| **UART2DATA** | `0x900 + 0x1b*4 = 0x9CC` | L574 | [7:0] DAT | §6.2 L437–442：写=加载到发送缓冲；读=从接收缓冲读 |
| **U2KEYCON** | `0x900 + 0x1c*4 = 0x9D0` | L575 | 键控 / 复位键匹配 | 手册未在本版本单列，**本工程不使用**；保留 SFR 地址 |

### 2.2 UART0 寄存器（默认 printf 通道）

| 寄存器 | 地址 | sfr.h 行号 | 与 UART2 差异 |
|:---|:---|:---|:---|
| **UART0CON** | `0x000 + 0x10*4 = 0x40` | L54 | 完全同 §6.2 描述 |
| **UART0CPND** | `0x44` | L55 | L55 标注 `SFR_WO`（写 0 清）；与 UART2 命名/位定义一致 |
| **UART0BAUD** | `0x48` | L56 | 同 §6.2 L430–435 |
| **UART0DATA** | `0x4C` | L57 | 同 §6.2 L437–442 |

> **代码里的间接使用**：`main.c` 的 `uart_putchar`（L92–97）只用 `UART0CON.BIT(8) (TXPND)` 与 `UART0DATA`，不读 RXPND（TX-only 调试串口）。

### 2.3 UART1 寄存器（已弃用但保留 SFR）

| 寄存器 | 地址 | sfr.h 行号 |
|:---|:---|:---|
| UART1CON | `0x000 + 0x30*4 = 0xC0` | L88 |
| UART1CPND | `0xC4` | L89 |
| UART1BAUD | `0xC8` | L90 |
| UART1DATA | `0xCC` | L91 |

> 工程内 `main.c` L228 显式 `FUNCMCON0 = 0xff000000` 关闭 UART1 映射；`FUNCMCON1 = 0xffffffff` 关闭 UART2 映射作为 reset 默认值。这是 boot ROM 默认把这两路功能映射都关闭的复用保险。

### 2.4 GPIO PortB 寄存器（PB1 / PB2 / PB3 都属此 Port）

| 寄存器 | 地址 | sfr.h 行号 | 用途 |
|:---|:---|:---|:---|
| **GPIOB** | `0x600 + 0x12*4 = 0x648` | L438 | 读 RX 引脚电平（软件 bit-bang 采样） |
| **GPIOBSET** | `0x600 + 0x10*4 = 0x640` | L436 | 写 1 置位 PB 输出 |
| **GPIOBCLR** | `0x600 + 0x11*4 = 0x644` | L437 | 写 1 清 PB 输出 |
| **GPIOBDIR** | `0x600 + 0x13*4 = 0x64C` | L439 | 0=输出 / 1=输入（手册 §3.2 L148 默认 0xFF 全输入） |
| **GPIOBDE** | `0x600 + 0x14*4 = 0x650` | L440 | 1=数字 IO / 0=模拟 |
| **GPIOBFEN** | `0x600 + 0x15*4 = 0x654` | L441 | **1=功能 IO（被外设接管）/ 0=普通 GPIO**（手册 §3.2 L156 关键控制位） |
| **GPIOBPU** | `0x600 + 0x17*4 = 0x65C` | L443 | 10KΩ 上拉使能（仅输入有效） |

> 完整 PB 段在 `sfr.h` L436–448（DRV / PD / PU200K / PD200K / PU300 / PD300 未在本工程使用，列出仅作索引参考）。

### 2.5 FUNCMCON 寄存器（功能映射核心）

| 寄存器 | 地址 | sfr.h 行号 | 关键位域 | 含义（手册 §3.3） |
|:---|:---|:---|:---|:---|
| **FUNCMCON0** | `0x000 + 0x07*4 = 0x1C` | L44 | [27:24] UT1TXMAP / [31:28] UT1RXMAP / [15:12] UT0RXMAP / [11:8] UT0TXMAP / [7:4] SPI0MAP / [3:0] SD0MAP | §3.3 L162–170 |
| **FUNCMCON1** | `0x000 + 0x08*4 = 0x20` | L45 | **[11:8] UT2RXMAP / [7:4] UT2TXMAP / [15:12] SPI1MAP / [3:0] FMOSCMAP** | **§3.3 L172–177：本工程 UART2 关键寄存器。`UT2TXMAP=2` → G2 → PB2；`UT2RXMAP=2` → G2 → PB1** |
| FUNCMCON2 | `0x24` | L46 | TMR3/4/5 PWM / Capture / IIS / IR / IIC / DVP | 与 UART 无关 |
| FUNCMCON3 | `0x27` (注：FUNCMCON3 在 SFR0_BASE+0x3f*4) | L102 | PDM / MPDM | 与 UART 无关 |

### 2.6 CLK / GPIO 时钟域（本工程用到的控制位）

| 寄存器 | 地址 | sfr.h 行号 | 涉及位 | 手册含义 |
|:---|:---|:---|:---|:---|
| **CLKCON0** | `0x000 + 0x19*4 = 0x64` | L63 | bit 4–6 SPLL 选择 / bit 3 sysclk sel / bit 30 等 | §3.4 L199–219 时钟切换 |
| **CLKCON2** | `0x300 + 0x2a*4 = 0xA68` | L280 | bit 24–29 x26m_div 分频 / bit 8–12 spll_div | §3.4 |
| **PWRCON0** | `0x300 + 0x1d*4 = 0xA74` | L266 | bit 20 PMU normal | §3.4 电源域 |
| **RTCCON3** | `0x300 + 0x33*4 = 0xACC`（SFR9） | L598 | bit 0 VDDBT enable | §3.4 电源域 |
| **LVDCON** | `0x300 + 0x1e*4 = 0xA78` | L267 | bit 30 关闭 | §3.4 |

> **注**：BT892X UART 的时钟源是 **系统时钟 24 MHz**（来自 `CLKCON0` SPLL），由 `UARTxCON[5] CLKSRC=0` 选通（手册 §6.2 L414）。这与 Timer 用的 `x26m_div_clk=1MHz`（`CLKCON2[29:24]` 见 `main.c` L231）**是两套时钟**，本工程在 `test_uart.md` §1.2 已明确强调。

---

## 3. 引脚定义与复用

### 3.1 UART2 引脚（G2 映射）

`PB2 = TX2-G2`，`PB1 = RX2-G2`（见 `bt892x_pinfunction.md` §4.2 L115–116 与 §8.1 L204 "UART2 TX/RX: PB1, PB2"）。

| PAD | 角色 | 复用名 | 所在 Group | 手册条目 |
|:---|:---|:---|:---|:---|
| **PB2 / WK3** | UART2 **TX** | `TX2-G2(RX)` —— 当 RXMAP=0x3 时 TX 复用 RX | G2 | §4.2 L116 |
| **PB1 / WK2** | UART2 **RX** | `RX2-G2` | G2 | §4.2 L115 |
| **PB3 / USBDP** | UART0 TX（默认调试串口） | `TX0-G3(RX)` | G3 | §4.2 L117 |
| PB0 / WK1 | 未用 | — | — | §4.2 L114 |
| PB4 / USBDM | 未用 | — | — | §4.2 L118 |
| PB5 / WKO | 10S Reset 主唤醒 | — | — | §4.2 L119 |

### 3.2 FUNCMCONx 映射值（工程实际配置）

| 寄存器位域 | 含义 | 工程值 | 含义解读 | 引用 |
|:---|:---|:---|:---|:---|
| `FUNCMCON1[7:4] UT2TXMAP` | UART2 TX 映射 Group | **2** | 选 G2 → PB2 | `uart_hal.c` L141, `test_uart.c` L24 |
| `FUNCMCON1[11:8] UT2RXMAP` | UART2 RX 映射 Group | **2** | 选 G2 → PB1 | 同上 |
| `FUNCMCON0[11:8] UT0TXMAP` | UART0 TX 映射 Group | **3** | 选 G3 → PB3 | `main.c` L181 `FUNCMCON0=(7<<12)|(3<<8)` |
| `FUNCMCON0[15:12] UT0RXMAP` | UART0 RX 映射 Group | **7** | 选 G7 = 与 TX 同脚（单线模式需 ONELINE=1） | `main.c` L181 |

> **注**：手册 §3.3 L165 与 `bt892x_pinfunction.md` §3 L82 都说"when RXMAP=0x7, TX pin will map to RX"——即 RXMAP=0x7 让 TX 复用 RX 单线工作。本工程 `UART0CON` 没有设 `ONELINE`（bit 6），所以 PB3 只是单向 TX（不需要 RX），详见 `main.c` L167–182 `uart0_mapping_sel`。

### 3.3 引脚冲突与约束（关键）

| 引脚 | 冲突外设 | 冲突源 | 解决办法 |
|:---|:---|:---|:---|
| **PB1** | **TMR3 PWM（`PWM1-T3-G1`）/ TMR3CAP_G4 / IR_G4 / SPI1CLK-G3 / IIC_CLK-G3/G4 / SDCLK-G2 / HSTRX-G7 / FMOSC-G4 / IISMCLK-G2/G3 / AUXL1 / ADC3** | `bt892x_pinfunction.md` §4.2 L115 + §8.6 L245 | UART2 测试期间关闭 TMR3 PWM；`main.c` L52 注释明确 `TEST_UART_EN` 与 `TEST_TIMER_PWM_EN` 不能同开 |
| **PB2** | **TMR3 PWM（`PWM2-T3-G1`）/ SPI1DO-G3 / IIC_DAT-G3 / SDDAT0-G2 / HSTRX-G2 / IISSCLK-G3 / IISDI-G2 / AUXR1 / ADC4** | §4.2 L116 + §8.6 L246 | 同上；且与软件 bit-bang UART 共享（不能软/硬同开） |
| **PB3** | USB DP（升级口）/ SPI0DO-G3 / IIC_CLK-G8 / HSTRX-G3 / PWM0-T3-G2 / SDDAT0-G5/SDCMD-G6 | §4.2 L117 + §6 L177 | **勿改 PB3 复用**，否则失去下载口 |
| **PB5 / WKO** | 10S Reset 主唤醒源 | §4.2 L119 + 附录 L332 | 不要用作 GPIO |
| **PG1/PG2/PG4/PG5** | SPI-Flash / MCP 默认信号 | §4.5 L147–152 + 附录 L334 | 不要随意占用 |

### 3.4 上拉 / 下拉选择

- `GPIOBPU=1`（10KΩ 上拉，§3.2 L150）：用于 PB1（RX）输入模式，避免浮空误触发 start bit。
- `GPIOBPU300=1`（300Ω 强上拉，本工程未用）。
- `GPIOBDIR`：TX=0 输出 / RX=1 输入；HAL 版默认都设输入（详见 §6.3 注释行 `soft_init` L34–38 与 §8.2）。

---

## 4. 初始化原理（为什么这样配）

### 4.1 时钟源

`UART2CON[5] CLKSRC=0`（手册 §6.2 L414）：选 **系统时钟**（24 MHz，由 `set_sys_clk(SYS_24M)` 在 `main.c` L241 配置；详见 `main.c` L182–220 与 §1.2 表）。这与 Timer 用 `x26m_div_clk=1MHz`（`main.c` L231）完全不同源，因此 `delay_us` 的 1µs 基准不适用于 UART 波特率计算——波特率必须直接基于 Fsys。

### 4.2 方向 / 上下拉 / 复用的因果链

要让 UART2 在 PB1/PB2 工作，必须完成 4 步串联：

1. **`FUNCMCON1` 选 Group**（手册 §3.3 L172–177）：把 UT2TXMAP/UT2RXMAP 都写 `2`，告诉芯片"UART2 的 TX/RX 物理引脚选 G2 这一组"。
2. **`GPIOBFEN |= BIT(n)`**（§3.2 L156）：把 PB1/PB2 切换为"功能 IO"，让硬件接管这两个引脚——若 `FEN=0`，它们只能当普通 GPIO。
3. **`GPIOBDE |= BIT(n)`**（§3.2 L155）：切到数字 IO（关闭模拟通路，否则信号被模拟 PAD 屏蔽）。
4. **`GPIOBDIR`**：TX 设输出（`DIR=0`），RX 设输入（`DIR=1`）。

`GPIOBPU |= BIT(n)` 是工程经验性附加：RX 浮空容易被噪声误触发，10K 上拉让 idle 状态稳定在 HIGH（与 UART stop bit 一致）。HAL 版进一步把 **TX 也默认设为输入**（`soft_init` L39–40），仅在 `soft_putc` 临时切输出，原因是 §6.3 注释解释：避免与外接 RX 源（PC USB-TTL）形成"双 driver 拉锯"——TX 输出 idle HIGH 与对端 start bit 的 LOW 冲突，会在线与上产生噪声。

### 4.3 波特率公式推导

手册 §6.2 L430–435：  
`baud = Fsys / (BAUD + 1)`  
其中 BAUD 是 16-bit 无符号整数。

工程里把 TX 与 RX 都用同一 BAUD：`UART2BAUD = (baud_val<<16) | baud_val`。

实际例子：Fsys=24 MHz，目标 115200 bps：
- `baud_val = 24_000_000 / 115_200 - 1 ≈ 207.33`（HAL 用 `(24000000+baud/2)/baud - 1` 做四舍五入，`uart_hal.c` L159；`test_uart.c` L39 写死 207）。
- 实际波特率 = `24_000_000 / (207+1) = 115_384 bps`，误差约 +0.16%，115200 的 UART 接收器允许 ±2.5% 误差，**安全**。
- 9600 的例子：`24_000_000 / 9600 - 1 = 2499`，但本工程软 UART 用 tmr_inc=1MHz 的 `delay_us`，**不通过 `UART2BAUD`**——见 §4.4。

### 4.4 时钟源差异的工程后果

| 模块 | 时钟源 | 频率 | 用途 |
|:---|:---|:---|:---|
| **UART2** | 系统时钟（`CLKCON0` SPLL） | 24 MHz | 硬件采样/发送时钟 |
| Timer2 / delay_us | x26m_div_clk = 26 MHz / 26 ≈ 1 MHz（`main.c` L231） | 1 MHz | 软件定时（软 UART 用此作 104µs/bit） |
| TMR3 PWM | 系统时钟或 x26m_div_clk 可选 | 视 FUNCMCON2 配置 | PWM 输出 |

→ **硬件 UART 与软件 bit-bang 的"波特率"含义不同**：硬件基于 Fsys，软件基于 `delay_us` 的 µs 数。

---

## 5. 初始化操作步骤

`test_uart.c` 在 HAL 重构后已不持有 init 函数本体——所有 5 个测试入口调 `uart_hal_hw_init` / `uart_hal_soft_init`。下面以 **`uart_hal_hw_init`**（`uart_hal.c` L137–177）为权威参考，编号 step-by-step。

| Step | 寄存器操作 | 含义 / 手册依据 |
|:---|:---|:---|
| **1. 时钟准备** | 由 `set_sys_clk(SYS_24M)`（`main.c` L243）完成 | 把系统切到 24 MHz（SPLL）；不属 UART 自身 init，但 UART 波特率假设此前提 |
| **2. 关闭 boot ROM 默认映射** | `FUNCMCON0 = 0xff000000; FUNCMCON1 = 0xffffffff;`（`main.c` L230–231） | 复位所有复用 Group，避免与 boot ROM 默认配置冲突 |
| **3. 选 Group** | `FUNCMCON1 &= ~((0xF<<4)\|(0xF<<8)); FUNCMCON1 \|= (2<<4)\|(2<<8);`（`uart_hal.c` L140–141） | §3.3：UT2TXMAP=2、UT2RXMAP=2 → G2 → PB2/PB1 |
| **4. PB2 → UART2 TX** | `GPIOBFEN \|= UART_TX_PIN; GPIOBDE \|= UART_TX_PIN; GPIOBDIR &= ~UART_TX_PIN; GPIOBPU \|= UART_TX_PIN;`（`uart_hal.c` L144–147） | §3.2：FEN=1（功能 IO）/ DE=1（数字）/ DIR=0（输出）/ PU=1（10K 上拉） |
| **5. PB1 → UART2 RX** | `GPIOBFEN \|= UART_RX_PIN; GPIOBDE \|= UART_RX_PIN; GPIOBDIR \|= UART_RX_PIN; GPIOBPU \|= UART_RX_PIN;`（`uart_hal.c` L150–153） | §3.2：DIR=1（输入），其余同上 |
| **6. 配波特率** | `u32 baud_val = (24000000 + baud/2)/baud - 1; UART2BAUD = (baud_val<<16)\|baud_val;`（`uart_hal.c` L159–160） | §6.2：TX/RX 同 divisor；24 MHz → 115200（baud_val=207）|
| **7. 使能 + 接收使能** | `UART2CON = BIT(7)\|BIT(0);`（`uart_hal.c` L163） | §6.2：UTEN=1 + RXEN=1 |
| **8. 稳定延时** | `delay_ms(10);`（`uart_hal.c` L164） | 工程经验：等 UART 时钟稳定 |
| **9. 冲洗 RX** | `while (UART2CON & BIT(9)) { (void)UART2DATA; UART2CPND = BIT(9); }`（`uart_hal.c` L167–170） | §6.2：使能后可能有毛刺；显式清 RXPND（HAL 版比早期多了 `UART2CPND = BIT(9)`，否则残留 PND 会让下次 getc 误读）|
| **10. printf 还原** | `my_printf_init(uart_putchar);`（`uart_hal.c` L176） | 防 `console` 测试残留导致 printf 走到非 UART0 |

---

## 6. 代码详解

### 6.1 `main.c` 中 UART0 默认 putchar（L93–98）

```c
 93 AT(.com_text.uart)
 94 void uart_putchar(char ch)
 95 {
 96     while (!(UART0CON & BIT(8)));   // §6.2 TXPND=1
 97     UART0DATA = ch;                  // 写 DATA 启动发送
 98 }
```

**逐段解释**：

- L93 `AT(.com_text.uart)`：链接段属性，把该函数放到 ROM 兼容的特定 section（避免被 boot ROM 链接器丢弃），与 `tick_get` / `delay_us` 等基础函数的处理方式一致。
- L96 `while (!(UART0CON & BIT(8)))`：阻塞等 TXPND=1。手册 §6.2 L411：TXPND=0 表示"未发送完 1 字节"。手册 §6.2 L428 补充："写 UART0DATA 也会清除 TXPND"，所以写入后 TXPND 自动回到 0，等到再次置 1 才表示本字节发完。**注意** 这里只等一次（与 `uart_hal_hw_putc` 等两次不同），因为 printf 字符流是连续的，下一个字符的 while 自然就把"上一字节发完"隐含处理了。
- L97 `UART0DATA = ch`：把字符写入数据寄存器，立即启动发送。

> 这里没有 RX 相关代码——UART0 是单向 TX-only 调试口，PC 只接收不发送。

### 6.2 `main.c` 中 `uart0_mapping_sel`（L167–182）

```c
167 void uart0_mapping_sel(void)
168 {
169     // close UART0 default PA7 print
170     GPIOAPU  &= ~BIT(7);
171     GPIOAFEN &= ~BIT(7);
172     GPIOADIR |= BIT(7);
173     GPIOADE  &= ~BIT(7);
174     FUNCMCON0 = (0xf << 12) | (0xf << 8);   // clear uart0 mapping
175
176     // USB PB3 print
177     GPIOBDE  |= BIT(3);
178     GPIOBPU  |= BIT(3);
179     GPIOBDIR |= BIT(3);
180     GPIOBFEN |= BIT(3);
181     FUNCMCON0 = (7 << 12) | (3 << 8);       // RX0 Map To TX0, TX0 Map to G3
182 }
```

**逐段解释**：

- L170–173：关闭 PA7 上 boot ROM 默认的 UART0 打印（手册 §3.2 L148–156）。把 PA7 的上拉/FEN/DIR/DE 全部置为禁用或输入，避免遗留电平干扰。
- L174：把 `FUNCMCON0[15:12]` 与 `[11:8]` 都写 0xF，对照手册 §3.3 L167–168 "1111=清除"——**先清**旧映射再写新映射，是 SFR 复用位的标准操作。
- L177–180：把 PB3 配成数字 IO + 输入方向 + 上拉 + 功能 IO。`DIR|=BIT(3)` 是把方向设**输入**（手册 §3.2 L148：DIR 0=输出/1=输入），但 L181 的 mapping 让 PB3 做 TX，所以这里 DIR 的写法其实是历史遗留——实际 TX 由硬件驱动，并不要求 DIR=0（`uart_putchar` 工作 OK 已验证）。
- L181：`FUNCMCON0 = (7<<12)|(3<<8)` —— `UT0TXMAP=3`（G3=PB3），`UT0RXMAP=7`（手册 §3.3 L165："0111=由 UT0TXMAP 选择"，即 RX 也指到 TX 同脚，配合 `ONELINE=1` 单线工作）。

### 6.3 `main.c` 中 `set_sys_clk`（L184–222）

```c
184 void set_sys_clk(u32 sys_clk)
185 {
186     u32 cpu_ie;
187     u32 uart_baud, spll_div = 0, spi_baud = 0;
188
189     if (sys_clk == SYS_24M) {
190         spll_div = 1;
191         spi_baud = 1;
192         uart_baud = (((24000000 + (UART_BAUD / 2)) / UART_BAUD) - 1);
193     } else if (sys_clk == SYS_48M) {
...
200     cpu_ie = PICCON & BIT(0);
201     PICCONCLR = BIT(0);                       // disable IRQ, switch system clock
202
203     if(UART0CON & BIT(0)) {                    // UTEN=1 ?
204         while (!(UART0CON & BIT(8)));          // 等待 UART0 当前字节发完
205     }
206     CLKCON0 &= ~(BIT(2) | BIT(3));             // sysclk → rc2m
...
210     CLKCON0 |= BIT(30);
...
215     CLKCON0 |= BIT(4);                         // spll select xosc52m_clk
216     CLKCON2 |= (spll_div << 8);
217     CLKCON0 |= BIT(3);                         // sysclk sel spll
218
219     UART0BAUD = (uart_baud << 16) | uart_baud; // 按新 Fsys 重算 baud
220     SPI0BAUD = spi_baud;
221     PICCON |= cpu_ie;
222 }
```

**关键点**：

- L192 / L196：根据目标系统时钟重新算 UART0 的 BAUD divisor（L17–18 `UART_BAUD=1500000`）。
- L201：关全局中断，避免切时钟过程被打断丢数据。
- L203–205：若 UART0 已使能，**等待当前字节发完**再切时钟——否则波特率突变会让正在发的字节电平失真。
- L219：切完时钟立即按新 Fsys 重算并写入 `UART0BAUD`。

→ UART2 没用这里面的"动态切"逻辑，因为 UART2 测试只在 `set_sys_clk` 之后跑，时钟已经稳定。

### 6.4 `uart_hal.c` 中 `uart_hal_hw_init`（L137–177）

> **2026-07-17 HAL 重构后**：本函数是**唯一**的硬件 UART2 init 入口，`test_uart.c` 不再持有 init 实现（详见 §8.2 疑点 2 解决状态）。

代码见 §5 Step 1–10 列表。差异点回顾：

- L159：`baud_val` 计算改公式化（支持任意 baud 参数传入）。
- L167–170：清 RXPND 循环里**额外**加 `UART2CPND = BIT(9)`，修复残留挂起的潜在问题。
- L176：调 `my_printf_init(uart_putchar)` 还原默认 printf（防 console 测试残留，见 §6.10）。

### 6.5 `uart_hal.c` 中 `uart_hal_hw_putc` / `_hw_getc`（L179–193）

```c
179 void uart_hal_hw_putc(u8 tx)
180 {
181     while (!(UART2CON & BIT(8)));   // 等 TX 空闲（§6.2 TXIPND=1）
182     UART2DATA = tx;
183     while (!(UART2CON & BIT(8)));   // 等发送完成（TXIPND 重新置 1）
184 }
185
186 u8 uart_hal_hw_getc(void)
187 {
188     u8 ch;
189     while (!(UART2CON & BIT(9)));   // 等 RXIPND=1
190     ch = (u8)UART2DATA;
191     UART2CPND = BIT(9);              // ★ 显式清挂起
192     return ch;
193 }
```

- **putc**（L179–184）：等两次 TXPND——保证上一字节完全离开移位寄存器再返回；比 `uart_putchar`（main.c L96 只等 1 次）更严格，确保 PC 端不会收到错位帧。
- **getc**（L186–193）：等 RXPND=1 后**显式**写 `UART2CPND = BIT(9)` 清挂起（手册 §6.2 L427 "写 1 清"）——这是关键的 bugfix：早期 `uart2_getc` 没显式清，下次循环 `while (!(RXPND))` 会因残留 PND 立刻通过，读到 DATA 寄存器的**同一字节**。

### 6.6 `test_uart.c` 中 `test_uart2_recv_run`（L109–198）

行缓冲 + Ctrl+C 退出 + per-byte echo（commit `acfb23e` 2026-07-22 加 echo），逻辑：

| L# | 作用 |
|:---|:---|
| 121–124 | 行缓冲（64 字节）+ rx_count / line_count 计数 |
| 127 | 阻塞读一字节（`uart_hal_hw_getc`，显式清 RXPND） |
| 132–136 | **每字节立即 echo** `RX[NNN] 0xXX 'C'`（per-byte echo，hex send 工具不需等 Enter）|
| 139–142 | 收到 `0x03`（Ctrl+C）→ 打印 `[Recv] Exit requested` 并 break |
| 145–159 | 收到 `\r` / `\n` → 整行 flush（行空时打 `<CR/LF only>`） |
| 161–167 | 收到 `0x08` / `0x7F` → 退格（删除前一个字符） |
| 169–172 | 其它 `< 0x20` 控制字符丢弃（避免破坏格式串） |
| 174–184 | 行未满 → 追加；行已满 → 立即 flush 再作为新行起点 |
| 190–195 | 退出前打印统计 |

> **已知限制**：轮询 `uart_hal_hw_getc` 在 printf 阻塞期间跟不上 115200 baud PC 突发，会丢字节。详见 [test_uart_ringbuf.md §1](test_uart_ringbuf.md) 与 §8.2 疑点 11。

### 6.7 `uart_hal.c` 中软件 bit-bang 实现（L28–128）

```c
 28 void uart_hal_soft_init(u32 baud)
 29 {
 30     GPIOBFEN &= ~(UART_TX_PIN | UART_RX_PIN);   // FEN=0 → GPIO
 31     GPIOBDE  |=  (UART_TX_PIN | UART_RX_PIN);
 32     GPIOBDIR |=  (UART_TX_PIN | UART_RX_PIN);   // ★ 默认输入
 33     GPIOBPU  |=  (UART_TX_PIN | UART_RX_PIN);
 34     ...
 47     my_printf_init(uart_putchar);
 48 }
 54 void uart_hal_soft_putc(u8 tx)
 55 {
 56     GPIOBDIR &= ~UART_TX_PIN;        // 临时设 PB2 输出
 57     GPIOBSET  = UART_TX_PIN;         // idle HIGH
 58     GPIOBCLR = UART_TX_PIN; delay_us(SOFT_BIT_US);  // start
 59     for (i = 0; i < 8; i++) {
 60         if (tx & (1u << i)) GPIOBSET = UART_TX_PIN;
 61         else                GPIOBCLR = UART_TX_PIN;
 62         delay_us(SOFT_BIT_US);
 63     }
 64     GPIOBSET = UART_TX_PIN; delay_us(SOFT_BIT_US);  // stop
 65     GPIOBDIR |=  UART_TX_PIN;        // ★ 恢复输入
 66 }
```

- L30：清 FEN，与硬件 UART2 互斥（同一 PB2 同一时刻只能被一种功能接管）。
- L32：默认 DIR=输入（DIR 位写 1），比硬件版本（`test_uart.c` L25 把 PB2 设为输出）更安全，理由见 `test_uart_hal_refactor.md` §3 bug 4：避免与外接 RX 源形成双 driver 噪声。
- L56：putc 入口临时把 PB2 切输出，发完恢复输入。
- L58–63：start bit (LOW) + 8 data bit (LSB-first) + 每位 `delay_us(104)`。
- L64：stop bit (HIGH)。
- L51 `SOFT_BIT_US=104`（9600 8N1：1 bit = 1e6/9600 ≈ 104µs）。

> `uart_hal_soft_txrx`（L105–128）是 loopback 专用：边发 TX 边在每 bit 中心采 RX。"putc + getc 组合"在 loopback 会死锁——putc 完线停在 stop HIGH，getc 等 start bit 永远不来，详见 `test_uart_hal_refactor.md` §3 Step 4。

### 6.9 `uart_hal.c` 中 Console / printf 重定向层（L199–224）

```c
199 void uart_hal_console_init(u32 baud)
200 {
201     uart_hal_hw_init(baud);                    // 复用 hw_init
202
203     printf("\r\n===== BT892X UART2 Console (printf -> UART2) =====\r\n");   // 仍走 UART0
204     printf("UART2: TX=PB2, RX=PB1, 115200bps 8N1\r\n");
205     printf("Wiring: PB2->USB-TTL RX, PB1<-USB-TTL TX, GND-GND\r\n");
206
207     my_printf_init(uart_hal_console_putchar);  // ★ 切到 UART2
208     printf("Type chars in PC serial monitor; they will be echoed back:\r\n");
209 }
216 void uart_hal_console_putchar(char ch)
217 {
218     uart_hal_hw_putc((u8)ch);
219 }
```

- L201：复用 hw_init，所以 console_init 是 hw_init + 重定向两步合一。
- L203–205：在切走之前先用 UART0/PB3 打 banner，**告诉用户"接下来 printf 要切到 UART2"**——否则用户在 PB3 看不到任何 banner。
- L207：`my_printf_init` 把全局 putchar 函数指针指向 `uart_hal_console_putchar`（指向 ROM 0x8401c，见 `reset.S` L128）。之后所有 printf 走 UART2/PB2。
- L218：每字符走 `uart_hal_hw_putc`（含阻塞等 TXPND）。

> 这是 `my_printf_init` 的标准用法：先 `init` 注册回调，再 `printf`。回调内部调阻塞 putc 是合法的——printf 不是中断上下文。

> **历史变更（commit 940e728）**：早期版本把 banner 放在 `my_printf_init` 之前 print，所有 3 行 banner 走 UART0/PB3，但用户场景只接了 PB2/PB1 的 USB-TTL，看不到 banner 误以为卡死。改为先 init 再 printf → banner 全部从 UART2/PB2 出。

### 6.10 `test_uart.c` 中 `test_uart2_console_run`（L206–215）

HAL 重构后此入口直接调 `uart_hal_console_init(115200)`，banner 由 HAL 自带打印（§6.9）。原 `test_uart.c` 里独立的 `uart2_console_init()`（含 `uart2_test_init()` + `my_printf_init(uart2_console_putchar)`）已在 commit `629ed39` 删除——`test_uart2_console_run` 是唯一入口。

---

## 7. 测试步骤与预期现象

### 7.1 公共接线

| 通道 | 接线 |
|:---|:---|
| **UART0/PB3**（调试口，**必接**） | PB3 → USB-TTL RX；GND ↔ GND；串口助手 1.5 Mbps 8N1 |
| **UART2/PB2(TX) / PB1(RX)**（按测试选接） | 见各子节 |

### 7.2 `TEST_UART_EN` —— 硬件回环（`test_uart.c` L28–51）

| 项 | 内容 |
|:---|:---|
| 接线 | **跳线 PB2 ↔ PB1**（短接 TX 和 RX） |
| 代码位置 | `test_uart.c` L28–51 (`test_uart_run`) |
| 行为 | 自发自收 0x00~0xFF 共 256 字节；每发一字节等 RXPND=1，再读 UART2DATA 与发送值比对 |
| 串口预期（PB3 @ 1.5M） | `===== BT892X UART2 Loopback Test =====` → `UART2: TX=PB2, RX=PB1, 115200bps 8N1` → `Jumper: PB2(TX) <-> PB1(RX)` → `===== Result =====` → `Total: 256, Errors: 0` → `ALL PASSED!` |
| 实测引用 | `test_uart.md` §2.4 实测输出：`Total: 256, Errors: 0` |
| 逻辑分析仪预期（可选） | PB2 TX：start LOW + 8 数据 bit（LSB-first）+ stop HIGH；PB1 RX 与 PB2 完全镜像（loopback）；每 bit 宽 ≈ 24M/115200 ≈ 8.68µs；总帧 ≈ 87µs |

### 7.3 `TEST_UART_SEND_EN` —— 持续发送（`test_uart.c` L57–99）

| 项 | 内容 |
|:---|:---|
| 接线 | **PB2 → USB-TTL RX**；GND ↔ GND；**不接 PB1**（开路） |
| 代码位置 | `test_uart.c` L57–99 (`test_uart2_send_run`) |
| 行为 | 每 500ms 发一行：`[N] Hello UART2!`，N 从 0 递增 |
| PC 串口助手预期 | 115200 8N1，每 0.5s 收到一行 `[0] Hello UART2!` / `[1] Hello UART2!` / ... |
| 实测引用 | `test_uart.md` §2.3 表"持续发送 ✅ PC 串口助手正常收到" |
| LA 预期 | PB2 持续出现短帧，间隔 ~500ms |

> 备注：`test_uart2_send_run` L68–98 手工把每个字符 putc 出去而非用 printf——因为 `TEST_UART_SEND_EN` 默认 printf 还走 UART0；且不能依赖 printf 重定向（会破坏 banner 在 PB3 显示）。

### 7.4 `TEST_UART_RECV_EN` —— 逐行接收 + per-byte echo（`test_uart.c` L109–198）

| 项 | 内容 |
|:---|:---|
| 接线 | **USB-TTL TX → PB1**；GND ↔ GND；**不接 PB2**（开路） |
| 代码位置 | `test_uart.c` L109–198 (`test_uart2_recv_run`) |
| 行为 | **每字节立即 echo** `RX[NNN] 0xXX 'C'`（commit `acfb23e`）；同时维护 64B 行缓冲，遇 `\r`/`\n` 整行 `printf` `UART2 RX line #N: "..."`；遇 `0x03` 退出；遇 `0x08`/`0x7F` 退格 |
| PB3 串口预期 | PC 发 `hello\r\n` → PB3 打：<br>`RX[000] 0x68 'h'`<br>`RX[001] 0x65 'e'`<br>`RX[002] 0x6C 'l'`<br>`RX[003] 0x6C 'l'`<br>`RX[004] 0x6F 'o'`<br>`RX[005] 0x0D`<br>`UART2 RX line #0: "hello" (len=5)` |
| 长行 64+ 字符 | 自动 flush 前 64 字符再继续 |
| Ctrl+C (0x03) | `[Recv] Exit requested (0x03)` + `[Recv] Total bytes: N, total lines: M` |
| 实测引用 | `test_uart.md` §2.5（含完整预期输出）；per-byte echo 引入 115200 baud 突发丢字节风险，详见 [test_uart_ringbuf.md](test_uart_ringbuf.md) |
| LA 预期（可选） | PB1 RX 出现完整 UART 帧：start LOW + 8 bit + stop HIGH；bit 宽 ≈ 8.68µs |

### 7.5 `TEST_UART_CONSOLE_EN` —— printf 重定向 + 回显（`test_uart.c` L206–215）

| 项 | 内容 |
|:---|:---|
| 接线 | **全双工**：PB2 → USB-TTL RX；USB-TTL TX → PB1；GND ↔ GND |
| 代码位置 | `test_uart.c` L206–215 (`test_uart2_console_run`) |
| 行为 | 调 `uart_hal_console_init(115200)`：内部 `uart_hal_hw_init` + `my_printf_init(uart_hal_console_putchar)`，所有 printf 走 UART2/PB2；主循环 `uart_hal_console_getc → uart_hal_console_putchar` 回显；收到 `\r` 多发一个 `\n`（终端习惯） |
| 串口助手预期（115200 8N1） | 开机在 **PB2** 上收到 4 行 banner：`===== BT892X UART2 Console (printf -> UART2) =====` / `UART2: TX=PB2, RX=PB1, 115200bps 8N1` / `Wiring: PB2->USB-TTL RX, PB1<-USB-TTL TX, GND-GND` / `Type chars ...`；之后每敲一字符立即收到回显（**全部在 PB2**，不再走 PB3）|
| 实测引用 | commit `940e728` 修复后：banner + 回显均通过 |
| ⚠️ 副作用 | 该 build 内所有 printf 都已切到 UART2；切回其它 build 时需在 main.c 启动时调 `my_printf_init(uart_putchar)`（HAL `uart_hal_hw_init` 末尾已自动调）|

### 7.6 `TEST_UART_SOFT_EN` —— 软件 bit-bang（HAL 版）

> **说明**：`test_uart_soft.c/.h` 独立文件在 `test_uart_hal_refactor.md` §3 Step 5 已被删除。HAL 重构后**所有测试入口都集中在 `test_uart.c`**，软路通过 `test_uart_soft_run` 入口调 `uart_hal_soft_*`（与硬件版共用同一份 HAL 接口）。

| 项 | 内容 |
|:---|:---|
| 接线 | 跳线 PB2 ↔ PB1（同硬件版） |
| 代码位置 | `test_uart.c` L222–237 (`test_uart_soft_run`) |
| 行为 | 边发边采 TXRX（`uart_hal_soft_txrx`），256 字节 loopback |
| LA 预期 | PB2 TX 与 PB1 RX 完全镜像；bit 宽 = 104µs（9600 bps）；半 bit = 52µs |
| 实测 | commit `940e728`：256 字节 PASSED（Total: 256, Errors: 0）|

### 7.7 main.c 中相关宏定义

```
#define UART_BAUD           1500000                          // main.c L17
#define UART_BAUD_VAL       (((24000000+(UART_BAUD/2))/UART_BAUD)-1)   // L18

// #define TEST_UART_EN         1      // 硬件 UART2 回环
// #define TEST_UART_SEND_EN    1      // 硬件 UART2 持续发送
// #define TEST_UART_RECV_EN    1      // 硬件 UART2 接收 (轮询, 115200 burst 丢字节)
// #define TEST_UART_CONSOLE_EN 1      // 硬件 UART2 收发回显 + printf 重定向
// #define TEST_UART_SOFT_EN    1      // 软件 bit-bang 回环
```

注意 `main.c` L53 注释：**`TEST_UART_EN` 不能与 `TEST_TIMER_PWM_EN` 同开**（PB1/PB2 与 TMR3 PWM 冲突）；同理 5 个 `TEST_UART_*_EN` 互相之间一次也只能开一个（PB1/PB2 唯一性）。

---

## 8. 审查小节（对照手册与引脚定义）

### 8.1 一致性核对结论（与手册一致的部分）

| 编号 | 检查项 | 手册依据 | 工程实现 | 一致性 |
|:---|:---|:---|:---|:---|
| 1 | `UART2CON.BIT(0) UTEN` 与 `BIT(7) RXEN` 是模块使能位 | §6.2 L419、L412 | `UART2CON = BIT(7)\|BIT(0);`（`test_uart.c` L43；`uart_hal.c` L163） | ✅ |
| 2 | `BIT(8) TXPND` / `BIT(9) RXPND` 是只读状态位 | §6.2 L411、L410 | `while(!(UART2CON & BIT(8)))` / `while(!(UART2CON & BIT(9)))`（多处） | ✅ |
| 3 | 波特率公式 `Fsys / (BAUD+1)`，TX/RX 分高低 16-bit | §6.2 L430–435 | `UART2BAUD = (207<<16)\|207;`（`test_uart.c` L40） | ✅ |
| 4 | `FUNCMCON1[7:4]` UT2TXMAP / `[11:8]` UT2RXMAP，0010=G2 | §3.3 L174–177 | `(2<<4)\|(2<<8)`（`test_uart.c` L24；`uart_hal.c` L141） | ✅ |
| 5 | G2 映射到 PB2/PB1 | `bt892x_pinfunction.md` §4.2 L115–116 | `GPIOBFEN/DE/DIR/PU` 配置对应 BIT(2)/BIT(1)（多处） | ✅ |
| 6 | GPIO DIR：0=输出/1=输入 | §3.2 L148 | `GPIOBDIR &= ~BIT(2)`（TX 输出）；`GPIOBDIR \|= BIT(1)`（RX 输入） | ✅ |
| 7 | GPIO FEN：1=功能 IO / 0=GPIO | §3.2 L156 | 硬件版 `GPIOBFEN \|= BIT(2/1)`；软件版 `GPIOBFEN &= ~BIT(2/1)` | ✅ |
| 8 | UART 时钟源 CLKSRC=0 = 系统时钟 | §6.2 L414 | 工程默认 `CLKSRC=0`（未显式写寄存器，依赖 reset 默认） | ✅ |
| 9 | 24 MHz / 115200 = 207.x → 选 207 | 计算 | `uart2_test_init` 写 207；HAL 版 `(24M+baud/2)/baud - 1` | ✅ |
| 10 | UART0 默认 putchar 等 TXPND | §6.2 L411 | `while(!(UART0CON & BIT(8)))`（`main.c` L95） | ✅ |
| 11 | UART0 TX 映射 G3=PB3 | `bt892x_pinfunction.md` §4.2 L117 + §3.3 | `(3<<8)`（`main.c` L181） | ✅ |
| 12 | `my_printf_init` 切 printf 通道 | `clib.h` L10 + `reset.S` L128（ROM 0x8401c） | `my_printf_init(uart_putchar)` / `my_printf_init(uart2_console_putchar)` | ✅ |
| 13 | 写 UARTxDATA 自动清 TXPND 但**不**自动清 RXPND | §6.2 L428 + `uart_hal.h` L18–19 注释 | HAL 版 `uart_hal_hw_getc` 显式 `UART2CPND = BIT(9)`（L191） | ✅ |
| 14 | 软件 bit-bang 时 FEN=0 强制 GPIO | §3.2 L156 | `GPIOBFEN &= ~(UART_TX_PIN\|UART_RX_PIN)`（`uart_hal.c` L31） | ✅ |

### 8.2 已发现疑点（只记录，未要求改代码）

| 编号 | 疑点 | 位置 | 影响 | 备注 / 状态 |
|:---|:---|:---|:---|:---|
| 1 | **`test_uart.c` 的 `uart2_getc` 未显式清 RXPND** | `test_uart.c` 已不存在该函数 | — | **✅ 已解决**（commit `629ed39`）：`test_uart.c` 的 `uart2_getc` 删除，所有 RECV 走 `uart_hal_hw_getc`（`uart_hal.c` L186–193），显式 `UART2CPND = BIT(9)` |
| 2 | **`test_uart.c` 与 `uart_hal.c` 重复实现 `uart2_*` 函数** | `test_uart.c` / `uart_hal.c` | 两份几乎一致的初始化代码导致维护成本翻倍 | **✅ 已解决**（commit `629ed39`）：`test_uart.c` 的 5 个 `test_uart*_run` 全部调 `uart_hal_*`，`uart2_test_init / putc / getc / console_init / console_putchar` 全部删除 |
| 3 | `test_uart2_recv_run` 不清 RXPND，连续接收会"少读一次" | `test_uart.c`（已用 `uart_hal_hw_getc`）| — | **✅ 已解决**：同疑点 1，现在显式清 RXPND。但 commit `acfb23e` 加 per-byte echo 后**新发现一类问题**：轮询 `getc` 在 printf 阻塞期间跟不上 115200 baud PC 突发，会丢字节——见 [test_uart_ringbuf.md](test_uart_ringbuf.md) 设计文档（中断+ringbuf 方案）|
| 4 | **`UART1(PA6/PA7) G1` 物理损坏已弃用** | `test_uart.c` L10 注释；`test_uart.md` §0 | 工程影响：无——已迁移到 UART2。但手册里 UART1 仍占 SFR 空间 | 历史决策已落实；建议在 `main.c` L53 注释区也写一句"UART1 损坏已弃用，避免误改" |
| 5 | **PB1/PB2 与 TMR3 PWM 冲突** | `main.c` L53 注释；`bt892x_pinfunction.md` §4.2 L115–116 + §8.6 L245–246 | 工程已有约束（`TEST_UART_*` 与 `TEST_TIMER_PWM_EN` 互斥），但**没有运行时检查**——如果用户同时 `#define` 两个宏会静默冲突 | 建议增加编译期 `#if defined(TEST_UART_EN) && defined(TEST_TIMER_PWM_EN) #error ...` |
| 6 | `main.c` L167–182 `uart0_mapping_sel` 把 PB3 设 DIR=输入但作为 TX 使用 | `main.c` L179 `GPIOBDIR \|= BIT(3)` | 工程实测工作 OK（手册 §3.2 L148 DIR 0=输出/1=输入），原因可能是 TX 由外设驱动时 DIR 位不影响输出——但**理论上** 应设 DIR=0 输出 | 历史实现稳定，未观察到问题；建议加注释说明 |
| 7 | `UART2CON[5] CLKSRC` 默认 0 选系统时钟，但工程**未显式写**该位 | `uart_hal.c` L163 | 依赖 reset 默认值（手册 §6.2 L414 未明说默认值，但 §6.2 表格 Mode 列为 WR 即可读写） | 工程工作 OK；如未来需要切到 `uart_inc` 异步时钟源才需显式操作 |
| 8 | `FUNCMCON1 = 0xffffffff` 关闭全部映射（含 SPI1 / FMOSC） | `main.c` L231 | 启动时把 SPI1/FMOSC 也一并清掉——若 boot ROM 默认这些有映射会被覆盖 | 假设：boot ROM 默认值安全；如有问题可改为 `FUNCMCON1 = 0x0000ff00` 仅清 [11:4] 两位 |
| 9 | `UART2CON = BIT(7)\|BIT(0)` 是直接赋值不是 OR 操作 | `uart_hal.c` L163 | 假设 init 前 `UART2CON` 一定为 0；若 boot ROM 默认有任何残留位（如 TXPND/RXPND 只读位之外的可写位）会被清掉 | 工程未观察到问题；保守做法可用 `UART2CON \|= BIT(7)\|BIT(0)` 代替 |
| 10 | `uart_hal.c` L160 `UART2BAUD = (baud_val<<16)\|baud_val;` 写全字，破坏 RXBAUD | 同上 | 同上——全字写入 OK，前提是 baud_val 在 [0,65535] 内（115200 时为 207） | 无影响 |
| 11 | 软件 bit-bang 的 `delay_us(SOFT_BIT_US)` 是阻塞 104µs，**加上 GPIO 操作**实际略长——9600 实测波特率比理论稍低 | `uart_hal.c` L51–52；`test_uart_hal_refactor.md` §6 | LOOP 软+硬 256 字节 PASSED，但 §6 报告 RECV 软路 0x41 → 0xA1 偏移——与外部 PC UART 时序不完全匹配 | **仅限软 UART 外部接收**场景；硬件 UART2 走 Fsys 24MHz，baud 误差 < 0.2%。修法见 [test_uart_ringbuf.md §1.4](test_uart_ringbuf.md) |
| 12 | `my_printf_init(uart_putchar)` 在 `main.c` L252 才调，**之前**的 printf（如 `exception_isr`）若触发会使用 ROM 默认函数指针（NULL） | `main.c` L84–89 `exception_isr` 用 printf；`main.c` L252 `my_printf_init` | 启动早期若发生异常会因函数指针为 0 而 crash | ROM 默认行为需查 `reset.S`；建议在 `main.c` 最早阶段（WDT_DIS 后立即）就调 `my_printf_init` |

### 8.3 总结

本工程 UART 子系统的寄存器操作与手册 §6.2、§3.2、§3.3 完全一致；引脚映射与 `bt892x_pinfunction.md` §4.2 / §8.1 一致；时钟源选择正确（24 MHz 系统时钟）；波特率计算正确（115200 → BAUD=207）；TX/RX PND 处理符合手册（TXPND 写 DATA 自清 / RXPND 需显式清）。

**HAL 重构完成（commit `629ed39`）**：所有 `test_uart.c` 测试入口走 `uart_hal_*`，消除 init/putc/getc 重复实现。**疑点 1 + 2 + 3** 全部解决。

**新增已知问题（commit `acfb23e` 暴露）**：轮询 RECV 在 115200 baud 下掉字节——per-byte echo 把 printf 耗时放大，PC 突发 3–4 字节在 printf 阻塞期间被硬件 DATA 寄存器覆盖丢失。**已写设计文档**（[test_uart_ringbuf.md](test_uart_ringbuf.md)）但代码未合入；待用户测试通过后另起 commit。

其余疑点为历史稳定性遗留，不影响当前功能。