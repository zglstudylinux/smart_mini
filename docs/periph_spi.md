# BT892X SPI 外设工程说明文档（软件 bit-bang + 硬件 SPI1 + W25Q64 + Polling/INT/DMA 三模式）

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 外设定位

BT892X 内部集成两组 SPI 控制器：SPI0（手册 §7）和 SPI1（与 SPI0 共享寄存器结构）。本工程**只用 SPI1**，未启用 SPI0。SPI1 支持 3 线 / 2 线 / 2 位双向 / 4 位双向多种总线宽度，本工程固定用 **3 线 MSB-first Mode 0（CPOL=0, CPHA=0）**。

### 1.2 三种物理通路

| 通路 | 角色 | 特点 |
|---|---|---|
| **软件 bit-bang** | 完全用 GPIO 模拟 SPI | 不依赖 SPI1 控制器，纯 GPIO 翻转，时序靠 `delay_us(1)`。用来对照硬件路径 |
| **硬件 SPI1**（Polling/IT/DMA 三模式） | BT892X SPI1 控制器 | Polling：CPU 100% busy；IT：ISR 通知；DMA：硬件自动搬运，CPU 最闲 |
| **W25Q64 Flash 驱动层** | 基于上述两通路跑 Flash 命令 | 实现写使能、读/状态、读数据、页编程、扇区擦等 6 条标准命令 |

### 1.3 工程用途

本工程用于：
- **验证 SPI1 控制器在 BT892X 上的功能与时序正确性**（跳线回环 / 逻辑分析仪波形 / W25Q64 命令全集 / 4096 字节 Polling/INT/DMA 三方时间对比）
- **教学与维护**：5 个测试入口按"接法互斥"分组（同一时刻只能选一种接法），方便按需烧录对比
- **HAL 抽象基线**：5 个 `test_spi_*.c` 共用 `spi_hal.{h,c}`，未来换芯片只改 HAL 一层

### 1.4 涉及文件清单（相对路径均相对于 `d:\Code\smart_mini\minimax\smart_mini\`）

| 文件路径 | 角色 |
|---|---|
| `smart_mini/test/spi_hal.h` | **HAL 接口声明**（pin 宏 + soft/hw 原语 + W25Q64 驱动 + IT/DMA API） |
| `smart_mini/test/spi_hal.c` | **HAL 实现**（300 行：soft/hw 字节级 + W25Q64 命令 + IT/DMA 流程） |
| `smart_mini/test/test_spi_loop.c` | 测试入口 1：跳线回环（软 + 硬 256 字节验证） |
| `smart_mini/test/test_spi_wave.c` | 测试入口 2：LA 波形（持续发 0x55 给逻辑分析仪） |
| `smart_mini/test/test_spi_w25q64.c` | 测试入口 3：W25Q64 软/硬 18 个实验（JEDEC/状态/读写擦/保护/UID/SFDP） |
| `smart_mini/test/test_spi_soft_asm.c` | 测试入口 4：软件 bit-bang C vs ASM vs ASM-展开 速度对比 |
| `smart_mini/test/test_spi_timing.c` | 测试入口 5：Polling / Interrupt / DMA 三方时间对比（100KHz + 12MHz） |
| `smart_mini/test/test_common.h` | `TEST_LOG`、GPIO 操作宏（DE/DIR/FEN/SET/CLR/READ） |
| `smart_mini/main.c` | 5 个 `TEST_SPI_*_EN` 宏开关（main.c 第 60-67 行）+ dispatch（第 298-312 行） |
| `smart_mini/header/sfr.h` | **寄存器宏定义**：SPI1 在第 579-584 行、GPIOE 在第 450-462 行、FUNCMCON1 在第 45 行、PICEN 在第 324 行 |
| `smart_mini/header/int.h` | `IRQ_SPI_VECTOR = 20`（第 23 行） |
| `smart_mini/header/include.h` | `TICK_1US = 1`（第 7 行），1 µs/tick 基础 |
| `docs/BT892X_UserManual_Driver.md` | 手册 §3.2 GPIO / §3.3 FUNCMCONx / §7 SPI / §7.3 使用指南 |
| `docs/bt892x_pinfunction.md` | 引脚手册 §4.3 PORTE（PE0/PE4~PE7）/ §5.2 / §8.2 SPI 信号速查 |
| `docs/test_spi.md` | **本工程的实测测试报告**（Phase 1-5，5 阶段全部 PASSED） |
| `docs/test_spi_hal_refactor.md` | HAL 浅重构记录（性能漂移分析） |

---

## 2. 涉及寄存器逐个说明

下面所有"地址"按 `sfr.h` 中 `SFR_RW (SFRx_BASE + offset*4)` 展开。例如 `SPI1CON = SFR_RW (SFR9_BASE + 0x20*4)`，`SFR9_BASE = 0x900`，最终地址 `0x900 + 0x80 = 0x980`。

### 2.1 SPI1 控制类寄存器

| 寄存器 | 地址（绝对） | sfr.h 行号 | 关键位域 | 含义（手册章节） |
|---|---|---|---|---|
| **SPI1CON** | 0x980 | 579 | bit0 SPIEN / bit1 SPISM / bit3:2 BUSMODE / bit4 RXSEL / bit5 CLKIDS / bit6 SMPS / bit7 SPIIE / bit8 SPILF_EN / bit9 SPIMBEN / bit10 SPIOSS / bit16 SPIPND(R) | §7.2 第 466-483 行 |
| **SPI1BUF** | 0x984 | 580 | bit7:0 数据 | 写：加载发送；读：取接收（§7.2 第 497-501 行） |
| **SPI1BAUD** | 0x988 | 581 | bit15:0 分频值 | `Fsys / (BAUD + 1)`（§7.2 第 485-489 行）。239→100kHz；1→12MHz（Fsys=24MHz） |
| **SPI1CPND** | 0x98C | 582 | bit16 SPICPND(W) | 写 1 清除 SPIPND 挂起（§7.2 第 491-495 行） |
| **SPI1DMACNT** | 0x990 | 583 | bit10:0 字节数(W) | 写此寄存器即**启动 DMA**（§7.2 第 503-507 行） |
| **SPI1DMAADR** | 0x994 | 584 | bit20:0 缓冲区地址(W) | 起始地址（§7.2 第 509-513 行） |

### 2.2 SPI1CON 关键位域详细含义（手册 §7.2 第 466-483 行）

| 位 | 名称 | Mode | 默认 | 描述 | 本工程用法 |
|---|---|---|---|---|---|
| 16 | SPIPND | R | 0 | 收发挂起 | Polling 模式 `while(!(SPI1CON & BIT(16)))` 等待 |
| 13 | HOLDENSW | WR | 0 | 软件 Hold | 不动 |
| 12 | HOLDENTX | WR | 0 | 蓝牙发送时 Hold | 不动 |
| 11 | HOLDENRX | WR | 0 | 蓝牙接收时 Hold | 不动 |
| 10 | SPIOSS | WR | 0 | 采样边沿：0=与输出不同；1=同边沿 | 默认 0（Mode 0/3 适用） |
| 9 | SPIMBEN | WR | 0 | 多位总线使能 | 默认 0（3 线单 bit） |
| 8 | SPILF_EN | WR | 0 | LFSR 使能 | 不动 |
| 7 | **SPIIE** | WR | 0 | **SPI 中断使能** | Interrupt 模式 `SPI1CON \|= BIT(7)` |
| 6 | SMPS | WR | 0 | 输出边沿：0=下降沿输出；1=上升沿输出 | 默认 0（Mode 0：下降沿输出=上升沿采样） |
| 5 | CLKIDS | WR | 0 | 时钟空闲电平 = **CPOL**：0=低；1=高 | 默认 0（Mode 0） |
| 4 | **RXSEL** | WR | 0 | DMA/2 线方向：0=发；1=收 | DMA 接收 `\|=BIT(4)`；发送 `&=~BIT(4)` |
| 3:2 | BUSMODE | WR | 00 | 总线宽度：00=3 线；01=2 线；10=2 位双向；11=4 位双向 | 默认 00（3 线） |
| 1 | SPISM | WR | 0 | 主/从：0=主机；1=从机 | 默认 0（主机） |
| 0 | **SPIEN** | WR | 0 | SPI 使能 | `SPI1CON = BIT(0)` 启动 |

**Mode 0 默认配置**：SPISM=0 / BUSMODE=00 / CLKIDS=0 / SMPS=0 / SPIOSS=0 / SPIEN=1，所有这 6 位要么默认是 0，要么就是要置 1，所以代码可写 `SPI1CON = BIT(0)`（前提是其他位复位为 0 — 审查小节会提到）。

### 2.3 周边辅助寄存器

| 寄存器 | 地址 | sfr.h 行号 | 角色 |
|---|---|---|---|
| **FUNCMCON1** | 0x020 | 45 | bit15:12 SPI1MAP（手册 §3.3 第 172-177 行）：0001~0110=G1~G6；本工程写 `FUNCMCON1 \|= (0x4 << 12)` 选 G4 |
| **GPIOEDE** | 0x690 | 454 | bit=1 数字 IO（手册 §3.2 第 134-157 行） |
| **GPIOEFEN** | 0x694 | 455 | bit=1 功能 IO；bit=0 普通 GPIO |
| **GPIOEDIR** | 0x68C | 453 | 0=输出；1=输入 |
| **GPIOESET** | 0x680 | 450 | 写 1 置位 |
| **GPIOECLR** | 0x684 | 451 | 写 1 清除 |
| **GPIOE** | 0x688 | 452 | 读 = 输入电平；写 = 输出电平 |
| **PICEN** | 0x444 | 324 | 中断使能：`PICEN \|= BIT(IRQ_SPI_VECTOR)` 开 SPI 中断 |
| **TMR2CNT** | 0x0F0 | — | `tick_get()` 返回值，1 tick = 1 µs（依赖 main.c `timer2_init`） |

> **手册 §3.3 实际只列出 FUNCMCON1[11:8]=UT2RXMAP 和 [7:4]=UT2TXMAP 两个字段**（手册第 172-177 行），但工程代码与 sfr.h 都把 SPI1MAP 视为 FUNCMCON1[15:12] 字段（test_spi.md 第 92 行引用）。这是工程惯例与手册版本之间的不一致，详见 §8 审查小节。

---

## 3. 引脚定义与复用

### 3.1 PORTE 端口引脚定义（手册 §4.3 第 121-129 行）

| 引脚 | 复位状态 | 在本工程软 SPI 的角色 | 在本工程硬 SPI1 G4 的角色 | 同时存在的复用功能 |
|---|---|---|---|---|
| **PE4** | hiz / INPUT | CS（软件 GPIO 输出） | CS（软件 GPIO 输出，**不映射**） | PWM1-T3-G4 / DVP_PCLK_IN |
| **PE5** | hiz / INPUT | MISO / DO（软件 GPIO 输入） | MISO / **SPI1DI-G4** | ADC7 / SDCMD-G3 / FMOSC-G5 / PWM0-T4-G1 / IISSCLK-G2 / IIC_DAT-G6 / DVP_HSYNC |
| **PE6** | hiz / INPUT | CLK（软件 GPIO 输出） | CLK / **SPI1CLK-G4** | ADC8 / AUXL2 / SDCLK-G3 / RX0-G4 / HSTRX-G9 / FMOSC-G6 / PWM1-T4-G1 / IISLRCLK-G2/G3 / IIC_CLK-G5/G6 / DVP_VSYNC |
| **PE7** | hiz / INPUT | MOSI / DI（软件 GPIO 输出） | MOSI / **SPI1DO/SPI1DATA-G4** | ADKEY / ADC9 / AUXR2 / SDDAT0-G3 / TX0-G4(RX) / HSTRX-G4 / PWM2-T4-G1 / IISDO/DAT-G2/G3 / IIC_DAT-G5 |

### 3.2 SPI1MAP 与 FUNCMCON1 映射（手册 §5.2 / §8.2 / test_spi.md 第 91-95 行）

| SPI1MAP 字段值 | 含义 | 引脚 | 备注 |
|---|---|---|---|
| 0001 | G1 | PA3(CLK) / PA4(DO) / PA5(DI) | — |
| 0010 | G2 | PA5(DI) / PA6(CLK) / PA7(DO) | — |
| 0011 | G3 | PB0(DI) / PB1(CLK) / PB2(DO) | — |
| **0100** | **G4** | **PE5(DI) / PE6(CLK) / PE7(DO)** | **本工程选用** |
| 0101 | G5 | PF1(DI) / PF4(CLK) / PF5(DO) | — |
| 0110 | G6 | — | — |
| 0111~1110 | 保留/无效 | — | — |
| 1111 | 清除 | — | 关闭映射 |

**FUNCMCON1 写入公式**（spi_hal.c 第 63-64 行）：
```c
FUNCMCON1 &= ~(0xF << 12);   // 清 SPI1MAP 字段
FUNCMCON1 |=  (0x4 << 12);   // 选 G4
```

### 3.3 引脚冲突约束

1. **同一时刻 5 个测试只能选 1 个**（main.c 第 60-67 行注释）：LOOP / WAVE / W25Q64 / SOFT_ASM / TIMING 共用 PE4~PE7，烧录前必须选对应 `TEST_SPI_*_EN` 宏，否则同一引脚被两种逻辑争夺导致数据错乱。
2. **PE5/PE6/PE7 与 UART0/IIC/SD/Timer4 等多外设复用**：当使用 `TEST_SPI_*_EN` 时，**禁止同时开 `TEST_UART_EN`**（UART0 RX0-G4 / TX0-G4 复用 PE6/PE7）、**禁止同时开 `TEST_I2C_*_EN`**（IIC_CLK-G5/G6 复用 PE6/PE7）。
3. **PE4 兼 SD PG 脚**（pinfunction.md 第 126 行 `PE4/SPIDIN`），但 SD 控制器使用 PG1/PG2/PG4/PG5（§4.5 第 147-152 行），与 PE4 无复用冲突，**本工程下可安全用作 SPI CS**。
4. **PE7 兼 ADKEY 功能**（第 129 行 `ADKEY`）：若同时开 `TEST_ADKEY_*_EN` 会冲突，**禁止同时开**。
5. **PE5/PE6 还兼 FMOSC 输入**（PE5=FMOSC-G5, PE6=FMOSC-G6）：如果系统时钟选 `SYS_24M` / `SYS_48M`（main.c 第 187-197 行 `set_sys_clk` 选 XOSC52M，**不走 FMOSC**），则这两个脚可安全用作 SPI；否则 FMOSC 路径会与 SPI 冲突。

---

## 4. 初始化原理（为什么这样配）

### 4.1 时钟源

`main.c: set_sys_clk(SYS_24M)`（main.c 第 241 行）将系统时钟配置为 **24 MHz XOSC**（不走 RC2M），再经 `CLKCON2 |= (25 << 24)` 把 `x26m_div_clk` 配为 1 MHz（main.c 第 231 行），供 `TMR_INC` 与 IR/FMAM 使用。**SPI1 的 Fsys = 24 MHz**——`SPI1BAUD = 239` → 24M/240 = 100 kHz；`SPI1BAUD = 1` → 24M/2 = 12 MHz。

> SPI1 不需要单独的 clock gate（手册未列 SPI1 的 CLKGAT 字段）。上电默认即开放。

### 4.2 引脚方向

软 SPI：CS/CLK/MOSI 作**输出**（`GPIOEDIR = 0`），MISO 作**输入**（`GPIOEDIR = 1`）。
硬 SPI1：CLK/MOSI 作输出（SPI1 控制器内部驱动），MISO 作输入（控制器内部采样），CS 仍手动用 GPIO 控制（**SPI1 控制器不带 CS 硬件**——手册 §7 未列 CS 控制位）。

### 4.3 上下拉

CS 默认高（用 `GPIOESET = SPI_CS_PIN`），CLK 默认低（用 `GPIOECLR = SPI_CLK_PIN`），符合 Mode 0 空闲态。上下拉电阻（手册 §3.2 第 149-154 行）：本工程**未启用任何上拉/下拉**——CS 由 GPIOESET 拉高、Flash MISO 走总线内置上拉（视 Flash 板决定）。

### 4.4 引脚映射的因果关系

要使用 SPI1 的硬件信号，必须先把对应的 PAD 切到**功能 IO**（`GPIOEFEN = 1` + `GPIOEDE = 1`），同时 `FUNCMCON1[15:12] = SPI1MAP` 选具体分组（G1~G6）。如果不写 FUNCMCON1，SPI1 时钟信号不会出现在任何 PAD 上。本工程选 G4（PE5/PE6/PE7），所以三个 PAD 必须全部 `FEN=1, DE=1`。

**CS 例外**：CS 不在 SPI1MAP 涵盖的 3 根线里（手册 §8.2 第 212 行仅列 CS / DIN / DO / CLK 4 类信号到 PAD，但 SPI1MAP 字段只决定其中 3 根的映射），所以 CS 一直保持普通 GPIO，由 CPU 手动控制。

### 4.5 SPI1CON 控制寄存器位的因果关系

Mode 0 = CPOL=0 + CPHA=0 + 3 线 + 主机 + 下降沿输出 + 采样异边沿 = 6 个位都默认 0 + SPIEN=1。所以 `SPI1CON = BIT(0)` 就完成 Mode 0 配置；位 4（RXSEL）只在 DMA 接收时临时置 1；位 7（SPIIE）只在 IT 模式临时置 1；位 16（SPIPND）是只读状态位。

### 4.6 SPIPND 轮询因果链

写 `SPI1BUF = tx` 启动一字节传输 → 8 个 CLK 周期后硬件置 `SPI1CON[16] = 1` → CPU `while(!(SPI1CON & BIT(16)))` 醒来 → 写 `SPI1CPND = BIT(16)` 清挂起 → 读 `SPI1BUF` 拿 RX。这是 Polling 模式的最小时序。

### 4.7 DMA 因果链

手册 §7.3 第 533-540 行明确："先 RXSEL 确定方向 → 配时钟与时序 → 使能 SPI → 写 SPI1DMAADR → **写 SPI1DMACNT 启动 DMA** → 等 SPIPND → 读 RX"。所以写 CNT 是**触发器**，必须最后写。

### 4.8 中断因果链

CPU 配 `SPI1CON |= BIT(7)`（SPIIE）→ 注册 ISR 到 `IRQ_SPI_VECTOR=20` → `PICEN |= BIT(20)` 开 PIC。SPIPND 置位后 PIC 触发 ISR → ISR 写 `SPI1CPND = BIT(16)` 清挂起并设 `spi_hal_spi_done = 1` → 主循环 `while(!spi_hal_spi_done)` 退出。

---

## 5. 初始化操作步骤（step-by-step）

下面以**软件 bit-bang** 和 **硬件 SPI1 Polling/IT/DMA** 三种模式分别列出。

### 5.1 软件 bit-bang 模式（spi_hal_soft_init）

1. **关闭功能映射**：`GPIOEFEN &= ~(CS|CLK|MOSI|MISO)`，让 4 个脚都作普通 GPIO。
2. **数字使能**：`GPIOEDE |= (CS|CLK|MOSI|MISO)`。
3. **方向配置**：`GPIOEDIR &= ~(CS|CLK|MOSI)` 输出；`GPIOEDIR |= MISO` 输入。
4. **空闲态**：CS 拉高（`GPIOESET = CS`），CLK 拉低（`GPIOECLR = CLK`）。

> **不需要任何 SPI 控制器配置**，完全 GPIO 即可。FUNCMCON1/SPI1CON 完全不碰。

### 5.2 硬件 SPI1 Polling 模式（spi_hal_hw_init）

1. **FUNCMCON1 选 G4**：`FUNCMCON1 &= ~(0xF<<12); FUNCMCON1 |= (0x4<<12)`。
2. **CLK/MOSI/MISO 引脚功能化**：`GPIOEFEN |= CLK|MOSI|MISO; GPIOEDE |= same; GPIOEDIR &= ~(CLK|MOSI); GPIOEDIR |= MISO`。
3. **CS 引脚 GPIO 化**：`GPIOEFEN &= ~CS; GPIOEDE |= CS; GPIOEDIR &= ~CS; GPIOESET = CS`。
4. **写波特率**：`SPI1BAUD = baud`（239=100kHz / 1=12MHz）。
5. **使能 SPI**：`SPI1CON = BIT(0)`（Mode 0，其他位默认 0）。
6. **不需要清挂起**（reset 后 SPIPND 本来就是 0）。

### 5.3 硬件 SPI1 Interrupt 模式（spi_hal_hw_it_setup）

1. 先按 5.2 完成 SPI1 初始化（SPIEN=1）。
2. `SPI1CON |= BIT(7)` 置 SPIIE。
3. `register_isr(IRQ_SPI_VECTOR=20, spi_hal_spi_isr)` 注册 ISR。
4. `PICEN |= BIT(IRQ_SPI_VECTOR)` 开 PIC。
5. 之后每字节：`SPI1BUF = tx; while(!spi_hal_spi_done); return (u8)SPI1BUF`。
6. 退出：`spi_hal_hw_it_teardown()` 关 `PICEN &= ~BIT(20)`、清 `SPI1CON &= ~BIT(7)`。

### 5.4 硬件 SPI1 DMA 模式（spi_hal_hw_read_dma / write_dma）

1. 按 5.2 完成 SPI1 初始化（SPIEN=1）。
2. CPU 用 Polling 发命令字节 + 地址字节（手册 §7.3 第 533-540 行要求先手发命令/地址）。
3. **接收路径**：`SPI1CON |= BIT(4)`（RXSEL=1）→ `SPI1DMAADR = (u32)buf` → `SPI1DMACNT = len`（**写 CNT 启动 DMA**）→ 等 SPIPND → 清挂起 → `SPI1CON &= ~BIT(4)`。
4. **发送路径**：`SPI1CON &= ~BIT(4)`（RXSEL=0）→ `SPI1DMAADR = (u32)buf` → `SPI1DMACNT = len` → 等 SPIPND → 清挂起。
5. 拉高 CS（`spi_hal_cs_high()`）。

---

## 6. 代码详解

### 6.1 软件 bit-bang 收发一字节（spi_hal.c 第 36-48 行）

```c
36  u8 spi_hal_soft_byte(u8 tx)
37  {
38      u8 rx = 0;
39      for (int i = 7; i >= 0; i--) {
40          if (tx & (1 << i)) GPIOESET = SPI_MOSI_PIN;  /* §3.2: MOSI=1 */
41          else               GPIOECLR = SPI_MOSI_PIN;  /* §3.2: MOSI=0 */
42          delay_us(1);
43          GPIOESET = SPI_CLK_PIN; delay_us(1);         /* §3.2: CLK 上升沿 */
44          if (GPIOE & SPI_MISO_PIN) rx |= (1 << i);    /* §3.2: 中心采样 MISO */
45          GPIOECLR = SPI_CLK_PIN; delay_us(1);         /* §3.2: CLK 下降沿 */
46      }
47      return rx;
48  }
```

逐段解释：
- L39：`MSB-first` 发送，从 bit7 走到 bit0。
- L40-41：先在 CLK 拉低期间把 MOSI 置好（手册 §3.2 GPIOESET/CLR 写 1 置/清）。这保证"在上升沿到来前数据已建立"，满足 SPI Mode 0 的 setup time。
- L42：`delay_us(1)` 等数据稳定（约 1 µs @ 24 MHz 足以跨过 GPIO 翻转 + 任何毛刺）。
- L43：拉高 CLK，**等 1 µs**——这是数据采样窗口（Mode 0：CLK 上升沿主机采样 MISO）。从机也在此沿采样 MOSI。
- L44：CPU 此时**直接读 GPIOE 寄存器**，看到 MISO=1 则把当前 bit 写进 `rx`。这是软件态的中心采样（紧跟上升沿，避免读到下一 bit）。
- L45：拉低 CLK，**等 1 µs**——给下一 bit 留出时间（同时满足从机 hold time）。
- 整个 8 bit 一字节约 24 µs（实测 ~58 µs，含函数调用与 C 优化开销）。

### 6.2 硬件 SPI1 初始化（spi_hal.c 第 60-80 行）

```c
60  void spi_hal_hw_init(u32 baud)
61  {
62      /* 引脚映射 G4 (FUNCMCON1[15:12]=0x4) */
63      FUNCMCON1 &= ~(0xF << 12);
64      FUNCMCON1 |=  (0x4 << 12);
65
66      /* CLK/MOSI/MISO → 功能 IO；CS 用 GPIO 手动控制（§3.2）*/
67      GPIOEFEN |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
68      GPIOEDE  |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
69      GPIOEDIR &= ~(SPI_CLK_PIN | SPI_MOSI_PIN);   /* CLK/MOSI 输出 */
70      GPIOEDIR |=   SPI_MISO_PIN;                   /* MISO 输入 */
71
72      GPIOEFEN &= ~SPI_CS_PIN;         /* §3.2: CS 用普通 GPIO */
73      GPIOEDE  |=  SPI_CS_PIN;
74      GPIOEDIR &= ~SPI_CS_PIN;
75      GPIOESET  =  SPI_CS_PIN;         /* CS 空闲高 */
76
77      /* §7.2 SPI1BAUD = Fsys / (BAUD + 1) */
78      SPI1BAUD = baud;       /* 100kHz = 239, 12MHz = 1 */
79      SPI1CON  = BIT(0);     /* §7.2: SPIEN=1 (Mode 0 其他位默认 0) */
80  }
```

逐条解释：
- L63-64：清 SPI1MAP 字段后写 0x4=G4。`&= ~` + `|=` 模式避免影响 FUNCMCON1 其他字段（如 UART2 映射）。
- L67-68：把 3 个功能 IO 切到功能映射态（手册 §3.2 GPIOAFEN=1 + GPIOADE=1）。SPI1 控制器将接管这些 PAD。
- L69-70：尽管 SPI1 内部已经知道 CLK/MOSI 是输出、MISO 是输入，但手册 §3.2 要求 GPIOEDIR 必须配，否则 FEN 不生效。**这是一致性要求**，不是冗余。
- L72-75：CS 例外地保持普通 GPIO（FEN=0），并预拉高。
- L78：`SPI1BAUD = baud` 必须先于 `SPI1CON = BIT(0)` 写——手册 §7.3 第 533-540 行的 DMA 流程与此一致（虽然 Polling 未明确顺序，但 BAUD 在 SPIEN=0 期间写最安全）。
- L79：`SPI1CON = BIT(0)` 用 `=` 而非 `|=`——这是因为 Mode 0 要求所有其他位为 0（详细见 §8 审查小节）。**这是一种"假定其他位复位为 0"**，目前实测是对的。

### 6.3 硬件 SPI 收发一字节（spi_hal.c 第 82-88 行）

```c
82  u8 spi_hal_hw_byte(u8 tx)
83  {
84      SPI1BUF = tx;                       /* §7.2: 写启动 */
85      while (!(SPI1CON & BIT(16)));        /* §7.2: 等 SPIPND=1 */
86      SPI1CPND = BIT(16);                  /* §7.2: 写 1 清挂起 */
87      return (u8)SPI1BUF;                  /* §7.2: 读接收 */
88  }
```

L84：写 BUF 即启动 8 个 CLK 周期传输；L85 阻塞等到 SPIPND=1（手册 §7.2 第 470 行）；L86 写 1 清挂起（手册 §7.2 第 495 行）；L87 读 BUF 取 RX。

### 6.4 CS 控制（spi_hal.c 第 91-101 行）

```c
91  void spi_hal_cs_low(void)
92  {
93      GPIOECLR = SPI_CS_PIN;
94      delay_us(1);   /* SPI 时序要求：CS 拉低后稍作延迟 */
95  }
```

CS 拉低后插入 1 µs 延迟——这满足从设备的 CS setup time（Flash 通常 ≥ 20 ns）。**不是 SPI 协议强制要求**，是工程防御性延迟，让 CS 沿稳定后再开始 CLK。

### 6.5 Interrupt ISR（spi_hal.c 第 245-250 行）

```c
245  AT(.com_text.isr)
246  static void spi_hal_spi_isr(void)
247  {
248      SPI1CPND = BIT(16);     /* §7.2 清挂起 */
249      spi_hal_spi_done = 1;
250  }
```

L248：必须在读 BUF 之前清挂起（手册 §7.2 SPICPND 第 495 行）。**清挂起是 ISR 必须做的**——否则下次传输仍看到 SPIPND=1，主循环永远不退出。

### 6.6 DMA 接收（spi_hal.c 第 273-295 行）

```c
284  void spi_hal_hw_read_dma(u32 addr, u8 *buf, u32 len)
285  {
286      spi_hal_hw_read_dma_cmd(addr);
287      /* 切到 DMA 接收：RXSEL=1（§7.2 SPI1CON[4]=1）*/
288      SPI1CON |= BIT(4);
289      SPI1DMAADR = (u32)buf;
290      SPI1DMACNT = len;
291      while (!(SPI1CON & BIT(16)));     /* §7.2 等 SPIPND */
292      SPI1CPND = BIT(16);
293      SPI1CON &= ~BIT(4);               /* 恢复 RXSEL=0 */
294      spi_hal_cs_high();
295  }
```

L286：先手发命令（0x03 Read Data）+ 3 字节地址——这 4 字节走 CPU Polling。L288：切 RXSEL=1（手册 §7.2 第 480 行）。L289-290：先写 ADR，再写 CNT——手册 §7.3 第 538-539 行明确 CNT 是"启动器"。L291-292：等 SPIPND 并清挂起。L293：恢复 RXSEL=0（不影响后续命令字字节的 Polling 收发）。

### 6.7 test_spi_loop.c 回环测试主流程（第 23-57 行）

```c
23  static int run_soft_loopback(void)
24  {
25      int err;
27      printf("\n##### Phase 1: Software SPI Loopback (bit-bang) #####\n");
28      printf("PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO\n");
30      spi_hal_soft_init();
32      err = 0;
33      for (int v = 0; v <= 0xFF; v++) {
34          u8 rx = spi_hal_soft_byte((u8)v);
35          if (rx != (u8)v) { err++; ... }
36      }
37      printf("Soft Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);
38      return err;
39  }
```

跳线 PE7↔PE5 接成软件回环：MOSI（PE7）发的字节从 MISO（PE5）原样返回。遍历 0x00~0xFF 全部 256 个值验证无误码。

### 6.8 test_spi_timing.c 三方对比（第 51-205 行）

Phase 5 入口：先用 `timing_check_jedec()` 验证 W25Q64 在线（Manufacturer ID = 0xEF），然后两轮 4096 字节读：
- **100 kHz 轮**：`spi_hal_hw_init(239)`，分别用 Polling / Interrupt / DMA 三种模式读同一段 buffer，最后比对 3 个 buffer 字节全部一致。
- **12 MHz 轮**：`spi_hal_hw_init(1)`，重复一遍。
- 最后算 Polling/DMA 加速比并打印。

**关键代码片段**（Polling 路径第 73-84 行）：
```c
73  /* Polling（轮询）*/
74  spi_hal_cs_low();
75  spi_hal_hw_byte(0x03);
76  spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
77  t0 = tick_get();
78  {
79      u32 i;
80      for (i = 0; i < BLK; i++) buf_p[i] = spi_hal_hw_byte(0xFF);
81  }
82  spi_hal_cs_high();
83  t_poll = tick_get() - t0;
```

`t0 = tick_get()` 在 CS LOW 与命令字之后，保证计时**只包含数据字节传输**（不含命令/地址/CS 包络时间）。

---

## 7. 测试步骤与预期现象

### 7.1 测试开关宏（main.c 第 60-67 行）

```c
// #define TEST_SPI_LOOP_EN         1  // 跳线接法：软件回环 + 硬件回环
// #define TEST_SPI_WAVE_EN         1  // LA 接法：软件波形 + 硬件波形
// #define TEST_SPI_W25Q64_EN       1  // Flash 接法：软硬 W25Q64 全套
// #define TEST_SPI_SOFT_ASM_EN     1  // 纯 GPIO：C vs ASM vs ASM-展开 速度对比
// #define TEST_SPI_TIMING_EN       1  // Flash 接法：polling/INT/DMA 时间对比
```

每次只打开一个，对应 main.c 第 298-312 行的 `#ifdef ... #endif` dispatch。

### 7.2 Phase 1：跳线回环（TEST_SPI_LOOP_EN）

- **接线**：PE7 ↔ PE5（杜邦线短接），PE4/PE6 不接，**不接 Flash、不接 LA**。
- **构建选项**：`TEST_SPI_LOOP_PHASE`：
  - 0（默认）：全跑软+硬
  - 1：只软
  - 2：只硬
- **预期串口输出**：
  ```
  ##### Phase 1: Software SPI Loopback (bit-bang) #####
  PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO
  Wiring: jumper PE7 <-> PE5

  Soft Loopback: PASSED (0/256 errors)

  ##### Phase 2: Hardware SPI1 Loopback #####
  PE4=CS(GPIO), PE6=CLK, PE7=MOSI, PE5=MISO (G4)
  Wiring: jumper PE7 <-> PE5

  HW Loopback: PASSED (0/256 errors)
  ```
- **结论**：实测 ✅（test_spi.md §2.4 第 179-192 行）

### 7.3 Phase 2：LA 波形（TEST_SPI_WAVE_EN）

- **接线**：LA CH0 → PE6（CLK），CH1 → PE7（MOSI），**不接跳线、不接 Flash**。
- **构建选项**：`TEST_SPI_WAVE_PHASE`：
  - 1（默认）：软 SPI 发 0x55 循环
  - 2：硬 SPI1 发 0x55 循环
- **LA 配置**：CPOL=0 / CPHA=0 / MSB-first；采样率 ≥ 10 MS/s（软 SPI）或 ≥ 1 MS/s（硬 SPI @ 100 kHz）。
- **预期 LA 波形**：CH0 上 8 个 CLK 周期对应 CH1 上 `01010101`，每字节间 CS=高（CH0 空闲低，CH1 保持）。**LA 解码每个字节都是 0x55**。
- **结论**：实测 ✅（test_spi.md §3.3 第 227-235 行）

### 7.4 Phase 3：W25Q64 Flash（TEST_SPI_W25Q64_EN）

- **接线**：CS=PE4 / CLK=PE6 / DI=PE7 / DO=PE5 → W25Q64。
- **构建选项**：
  - `SPI_W25_RUN_MODE`：0=只软 / 1=只硬 / **2=软硬全跑（默认）**
  - `SPI_SW_W25_MODE` / `SPI_HW_W25_MODE`：0=单 exp / **1=全测**（含 Chip Erase ~20 s）/ 2=仅读（推荐调试先用）
  - `SPI_SW_W25_EXP` / `SPI_HW_W25_EXP`：单 exp 模式下选 1~10（HW）或 1/2/3/4/5/6/7/9（SW）
- **预期串口**（HW Exp1 JEDEC）：
  ```
  ===== [HW] Exp1: JEDEC ID =====
  JEDEC: 0xEF 0x40 0x17 OK
  ```
- **HW Exp6 擦除耗时实测**（test_spi.md §4.1 第 256 行）：
  | 擦除类型 | 命令 | 实测耗时 |
  |---|---|---|
  | Sector 4KB | 0x20 | 51 ms |
  | Block 32KB | 0x52 | 103 ms |
  | Block 64KB | 0xD8 | 165~169 ms |
  | Chip Erase | 0xC7 | **~18.5 s** |
- **HW Exp8 Fast Read 对比**：100 kHz 下 Standard (0x03) = 170979 ticks vs Fast (0x0B) = 171082 ticks（差距 ~103 ticks = 1 dummy byte 开销，**低速下 Fast Read 几乎无优势**——手册 §7.3 第 533 行提到 Fast Read 主要在高速时受益）。
- **HW Exp9 100K vs 12M 对比**：100 kHz = 341602 ticks vs **12 MHz = 9941 ticks**（4096 字节，**~34× 加速**）。
- **结论**：实测 ✅（test_spi.md §4.1 第 247-262 行）

### 7.5 Phase 4：C vs ASM 速度对比（TEST_SPI_SOFT_ASM_EN）

- **接线**：纯 GPIO，可选接 W25Q64（仅做 JEDEC 功能校验）。
- **构建选项**：无。
- **测试方法**：发 10000 字节 `0x55`，分别用 C / ASM 循环 / ASM 展开 三种实现计时。
- **预期串口**（test_spi.md §5.1 第 271-277 行实测）：
  ```
  C version:      584802 ticks (58.48 us/byte)
  ASM (loop):     582513 ticks (58.25 us/byte)   faster 0.39%
  ASM (unrolled): 597833 ticks (59.78 us/byte)   SLOWER 2.23%
  ```
- **关键结论**：
  - C 已足够好：`delay_us(1) × 3/bit × 8bit = 24 µs` 理论最小，实测 58 µs ≈ 2.4× 调用开销。
  - ASM 循环版仅快 0.39%（基本统计噪声）。
  - ASM 展开版反而慢 2.23%（`__asm__ volatile` 反复刷流水线 barrier 比省下的循环判断成本高）。
  - **HAL 重构后 C 慢 ~11%**（649360 ticks / 64 µs/byte），因跨 TU 调用无法 inline（test_spi_hal_refactor.md §3.3 / §5.2）。修复：编译加 `-flto`。

### 7.6 Phase 5：Polling/INT/DMA 三方时间对比（TEST_SPI_TIMING_EN）

- **接线**：W25Q64 Flash（同 Phase 3），CS=PE4/CLK=PE6/DI=PE7/DO=PE5。
- **构建选项**：无。
- **测试方法**：4096 字节读，分别在 100 kHz 和 12 MHz 下用三种模式测试，最后比对 3 buffer 一致性 + 算加速比。
- **预期实测数据**（test_spi.md §6.1 第 297-314 行）：

  **100 kHz 三方对比**：
  | 模式 | ticks | µs/byte | 吞吐 |
  |---|---|---|---|
  | Polling | 341641 | 83.40 | 12.0 KB/s |
  | Interrupt | 416957 | 101.79 | 9.8 KB/s |
  | DMA | 302815 | **73.92** | 13.5 KB/s |

  → DMA 仅比 Polling **快 13%**（低速时 SPI 硬件时钟主导，CPU 开销不显著）。

  **12 MHz 三方对比**：
  | 模式 | ticks | µs/byte | 吞吐 |
  |---|---|---|---|
  | Polling | 10099 | 2.46 | 407 KB/s |
  | Interrupt | 91949 | **22.44** | 45 KB/s |
  | DMA | 2533 | **0.61** | 1640 KB/s |

  → DMA **4.0× Polling**；**Interrupt 反而慢 9×**（ISR 进/出 + volatile 自旋开销主导）；3-way data match **OK**。
- **结论**：实测 ✅（test_spi.md §6.1）

  **工程建议**：
  - **高速 SPI（≥1 MHz）→ DMA 优先**
  - **低速 SPI + 偶尔传输 → Interrupt 省 CPU**
  - **阻塞小数据 → Polling 最简单**

### 7.7 串口打印配置

所有测试统一通过 UART0 @ 1.5 Mbps 打印（main.c 第 17-18 行 `UART_BAUD = 1500000`，第 239 行 `UART0BAUD` 配置；TX/RX 走 PB3/PB4 默认路径或经 `uart0_mapping_sel()` 切换）。

---

## 8. 审查小节（对照手册与引脚定义的一致性核对）

### 8.1 一致性核对结论（与手册 §7 / §3.2 / §3.3 完全对齐的部分）

| 操作 | 对照手册 / 寄存器位表 | 结论 |
|---|---|---|
| `SPI1BAUD = 239` → 100 kHz | §7.2 第 485-489 行：`Fsys / (BAUD+1)` = 24M/240 | ✅ 完全一致 |
| `SPI1BAUD = 1` → 12 MHz | §7.2 同上公式：24M/2 = 12M | ✅ 完全一致 |
| `SPI1CON = BIT(0)` 使能 + Mode 0 | §7.2 第 483 行 SPIEN + 默认其他位=0 | ✅ 在 reset 后成立 |
| `SPI1CON \|= BIT(7)` 开 SPIIE | §7.2 第 477 行 | ✅ |
| `SPI1CON \|= BIT(4)` RXSEL=1 | §7.2 第 480 行 | ✅ |
| `SPI1CPND = BIT(16)` 清挂起 | §7.2 第 495 行（写 1 清除） | ✅ |
| `SPI1BUF = tx` 启动 + `SPI1BUF` 读 RX | §7.2 第 501 行 | ✅ |
| 写 `SPI1DMACNT` 启动 DMA | §7.3 第 539 行（明确写此寄存器触发） | ✅ |
| `FUNCMCON1[15:12] = 0x4` 选 G4 | 工程惯例（手册 §3.3 第 172-177 行只列了 UT2RX/TXMAP 字段，未明确 SPI1MAP） | ⚠️ 见 §8.2 疑点 1 |
| 软 SPI 顺序（MOSI→delay→CLK↑→delay→采样→CLK↓） | 经典 Mode 0 时序 | ✅ |
| CS 用 GPIO 控制 | §7.3 未列 SPI 控制器 CS 控制位（手册 §7.2 也无 CS 字段） | ✅ SPI1 控制器不带硬件 CS |
| `IRQ_SPI_VECTOR = 20` | `int.h` 第 23 行 | ✅ |
| `delay_us(1)` 基于 `TMR2CNT = 1 µs/tick` | `main.c` 第 100-127 行 + `include.h` 第 7 行 `TICK_1US=1` | ✅ |

### 8.2 发现的疑点（仅记录，不要求改代码）

1. **FUNCMCON1 的 SPI1MAP 字段位置**：
   - 工程代码（spi_hal.c 第 63-64 行 + test_spi.md 第 92 行）使用 `FUNCMCON1[15:12] = SPI1MAP`，取值 0001~0110=G1~G6。
   - 但当前版本手册 §3.3 第 172-177 行只列出 `FUNCMCON1[11:8]=UT2RXMAP` 和 `[7:4]=UT2TXMAP` 两个字段。
   - **疑点**：手册与工程代码不一致。可能 SPI1MAP 实际位于其他 FUNCMCONx，或本版本手册缺漏。
   - **风险**：若手册版本更新后 SPI1MAP 字段迁移，所有 5 个测试会同时失败。
   - **处理建议**：下次升级手册时核对字段位置；或实测时若 G4 不工作，逐步排查其他 FUNCMCON 寄存器。

2. **SPI1CON = BIT(0) 假定其他位复位为 0**：
   - 当前代码用 `=` 直接赋值（spi_hal.c 第 79 行）。
   - **隐含假设**：bit1(SPISM)、bit3:2(BUSMODE)、bit5(CLKIDS)、bit6(SMPS)、bit10(SPIOSS) 在 reset 后都是 0，且本工程未在别处修改过这些位。
   - **风险**：若 boot loader 或别的初始化代码已写过 SPI1CON（比如把 SPIOSS=1），后续 `SPI1CON = BIT(0)` 会清掉那些设置。
   - **当前实测**：✅ 5 个测试全过，所以前提目前成立。
   - **建议**（不要求改）：改成 `SPI1CON = (SPI1CON & ~<其他位掩码>) | BIT(0)` 或先 `SPI1CON = 0; SPI1CON = BIT(0)`，更显式安全。

3. **C vs ASM 跨 TU 无法 inline 的性能漂移**：
   - HAL 重构后，C 版本从 `static` 内联变为跨 TU 外部调用（spi_hal.c 是单独 .c 文件）。
   - **实测**：C 版本从 58 µs/byte 漂到 64 µs/byte（test_spi_hal_refactor.md §3.3 第 119-125 行）。`__asm__ volatile` 不受影响（强制 inline）。
   - **结论反转**：之前 C ≈ ASM，现在 ASM 比 C **快 10%**。
   - **建议**：app.cbp 编译器选项加 `-flto` 关闭漂移（已在 test_spi_hal_refactor.md §5.5 记录，未实施）。

4. **CS 需手动控制，无硬件支持**：
   - SPI1 控制器手册 §7.2 / §7.3 都没有 CS 控制位。
   - **后果**：多从设备场景必须用 GPIO 或外部译码器，本工程软硬都手动控制 CS（spi_hal.c 第 91-101 行 `spi_hal_cs_low/high`）。
   - **当前实现**：每次字节前 `cs_low()`，字节后 `cs_high()`——如果应用层连续多字节，可只在外层包一次 cs_low/cs_high（手册 §7.3 第 524-526 行的"正常 1 位模式操作流程"默认每字节单独 CS 包络，但 W25Q64 的 Read Data 命令也支持 CS 持续拉低的多字节流式读）。

5. **5 个 SPI 测试共享 PE4~PE7 互斥**：
   - LOOP / WAVE / W25Q64 / SOFT_ASM / TIMING 都用 PE4~PE7。
   - **main.c 第 60-67 行注释明确**：一次只开一个。
   - **风险**：用户若同时开 2 个 `TEST_SPI_*_EN`，编译能过但运行时两套逻辑争引脚，数据错乱。
   - **建议**：未来可加编译期 `#if defined(TEST_SPI_LOOP_EN) + defined(...) > 1` 检查并 `#error`，但当前未实施。

6. **软件 SPI 的 24 µs 理论最小值 vs 实测 58 µs 差距**：
   - 理论 `delay_us(1) × 3 × 8 = 24 µs`，实测 58~64 µs，**约 2.4× 调用开销**。
   - **根因**：每次 `delay_us(1)` 内部 `tick_get()` + `tick_check_expire()` 循环判断 + 内联阈值常量计算（main.c 第 121-127 行）。
   - **建议**（不要求改）：若需极限 bit-bang 速率，可改成 `__asm__ volatile("nop")` 精确延时，但只在周期精确需求场景（如 > 5 Mbps）值得做。

7. **`spi_hal_hw_byte` 未保护并发**：
   - `SPI1BUF` 是单字节收发寄存器，若多线程同时调 `spi_hal_hw_byte`，字节会交错。
   - **本工程单线程**（main.c 第 341 行 `while(1);`），无并发风险。
   - **未来若上 RTOS**：需在 HAL 加 mutex 或换 DMA 队列。

8. **Phase 5 Polling 实测绝对值漂移**：
   - test_spi_hal_refactor.md §5.1：HAL 重构前 Polling 12 MHz = 10072 ticks，重构后 = 8047 ticks（**快 25%**）。
   - **解释**：可能是 cache 命中变化 / 编译器寄存器分配优化差异。
   - **结论**：相对加速比（DMA/Polling）从 3.97× 略降到 3.1×，但**关键结论"DMA 最快"不变**。

---

## 文档结束

**关键文件路径汇总**（便于后续维护者快速定位）：

- 测试报告：`d:\Code\smart_mini\minimax\smart_mini\docs\test_spi.md`
- HAL 重构记录：`d:\Code\smart_mini\minimax\smart_mini\docs\test_spi_hal_refactor.md`
- 手册：`d:\Code\smart_mini\minimax\smart_mini\docs\BT892X_UserManual_Driver.md`（§3.2 / §3.3 / §7.2 / §7.3）
- 引脚：`d:\Code\smart_mini\minimax\smart_mini\docs\bt892x_pinfunction.md`（§4.3 / §5.2 / §8.2）
- 寄存器：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\header\sfr.h`（SPI1 L579-584、GPIOE L450-462、FUNCMCON1 L45、PICEN L324）
- 中断向量：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\header\int.h`（`IRQ_SPI_VECTOR=20` L23）
- HAL 接口：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\test\spi_hal.{h,c}`
- 测试入口：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\test\test_spi_{loop,wave,w25q64,soft_asm,timing}.c`
- 主程序开关：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\main.c`（L60-67 注释 / L298-312 dispatch）