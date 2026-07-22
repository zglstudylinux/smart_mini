# GPIO 外设技术说明文档（BT892X / RISC-V）

> 芯片：中科蓝讯 BT892X（32-bit RISC-V，CodeBlocks 工程）
> 工程目录：`smart_mini/smart_mini/`
> 寄存器映射头：`smart_mini/header/sfr.h`
> 关联手册：`docs/BT892X_UserManual_Driver.md` §3 GPIO（行 134–243）、`docs/bt892x_pinfunction.md` §4 通用 IO
> 已通过测试报告：`docs/test_gpio.md`（PE4/PE5/PE6/PE7/PB1/PB2 1Hz 方波翻转实测通过）

---

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 外设概述

BT892X 的 GPIO（General Purpose Input/Output）是所有外设功能复用的基础硬件。每个 PAD 都可以被配置为：

- **纯数字 GPIO**（输入或输出，8 mA / 32 mA 两档驱动）
- **模拟 IO**（关闭数字电路，让出引脚给 ADC / AUX / 高压等模拟通道）
- **功能映射 IO**（把 PAD 交给 UART / SPI / I2C / PWM / IIS / SD / Timer 等外设，由 `FUNCMCONx` 选 Group）

BT892X 的 GPIO 控制器由 **6 组寄存器** 组成（GPIOA / GPIOB / GPIOE / GPIOF / GPIOG），分别挂在 SFR Group6（0x600–0x6FF）与 SFR Group7（0x700–0x73F）。每组 13 个寄存器，覆盖方向、数字使能、功能映射使能、上下拉、驱动能力、数据 / 置位 / 清除三类数据访问通路。手册 §3.1（行 136–140）列出三大特性：方向可配、上下拉可配、驱动 8 mA / 32 mA 可选。

### 1.2 本工程用途

本工程（`app.cbp`）是**寄存器级裸跑工程**，无独立 driver 层。GPIO 在本工程里承担两类职责：

1. **printf 调试口**（`main.c` 行 165–180 `uart0_mapping_sel()`）：把 PB3（`USBDP` / `UART0-G3`）通过 `FUNCMCON0 = (7<<12)|(3<<8)` 配置为 UART0 单线 TX（手册 §3.3 行 167–168 的 `0111=由 UT0TXMAP 选择` 含义）。
2. **GPIO 引脚物理可达性诊断**（`test/test_gpio.c`）：把开发板上确认能安全使用的 6 个引脚 PE4/PE5/PE6/PE7/PB1/PB2 拉成 1 Hz 方波，用逻辑分析仪观察。

### 1.3 涉及文件清单（相对路径，均相对工程根 `d:\Code\smart_mini\minimax`）

| 相对路径 | 作用 |
|---|---|
| `smart_mini/smart_mini/header/sfr.h` | GPIOA/B/E/F/G 寄存器宏定义（行 421–491）+ `FUNCMCON0/1/2/3`（行 44–46、102） |
| `smart_mini/smart_mini/header/include.h` | `UTX0MAP_PBx` / `URX0MAP_PBx` / `SYS_CLK` 等系统级宏 |
| `smart_mini/smart_mini/header/macro.h` | `BIT(n) = (1ul<<n)`（GPIO 位操作基础） |
| `smart_mini/smart_mini/main.c` | `uart0_mapping_sel()`（行 165–180）+ `#define TEST_GPIO_EN` 开关（行 50） |
| `smart_mini/smart_mini/test/test_gpio.c` | GPIO 引脚翻转诊断实现（1 Hz × 2 s / pin） |
| `smart_mini/smart_mini/test/test_gpio.h` | 测试入口声明 `void test_gpio_run(void)` |
| `smart_mini/smart_mini/test/test_common.h` | 共用宏：`TEST_LOG`、`TEST_GPIO_OUT/IN/HIGH/LOW/READ`（双层宏解决 `##` 拼接） |
| `smart_mini/docs/BT892X_UserManual_Driver.md` | GPIO 章节 §3（行 134–243） |
| `smart_mini/docs/bt892x_pinfunction.md` | 引脚复用表 §4.2 / §4.3（行 110–129） |
| `smart_mini/docs/test_gpio.md` | 已通过测试报告（PE4/PE5/PE6/PE7/PB1/PB2 实测方波） |

---

## 2. 涉及寄存器逐个说明

### 2.1 GPIO 端口寄存器组（手册 §3.2 行 141–157）

> 手册原文 "以 Port A 为例"。本工程涉及 A / B / E 三组寄存器，F/G 两组在 sfr.h 已定义但本工程尚未使用。每个端口寄存器组结构完全一致。

#### GPIOA 寄存器组（sfr.h 行 422–434）

| 寄存器名 | 地址 | sfr.h 行号 | 位宽 | 关键位域 | 含义（手册章节） |
|---|---|---|---|---|---|
| `GPIOASET` | 0x600 | 422 | 8b（7:0 有效） | `SET[7:0]` | **写 1 置位**对应 PAD 输出高电平；写 0 无动作（§3.2 行 146） |
| `GPIOACLR` | 0x604 | 423 | 8b | `CLR[7:0]` | **写 1 清除**对应 PAD 输出低电平；写 0 无动作（§3.2 行 147） |
| `GPIOA` | 0x608 | 424 | 8b | `DAT[7:0]` | 数据寄存器：**读**返回输入电平（与 IO 同步），**写**直接驱动输出（§3.2 行 145） |
| `GPIOADIR` | 0x60C | 425 | 8b | `DIR[7:0]` | 方向寄存器：**0=输出，1=输入**（复位默认 0xFF = 全输入，§3.2 行 148） |
| `GPIOADE` | 0x610 | 426 | 8b | `DE[7:0]` | **数字功能使能**：0=模拟 IO，1=数字 IO（复位默认 0xFF，§3.2 行 155） |
| `GPIOAFEN` | 0x614 | 427 | 8b | `FEN[7:0]` | **功能映射使能**：0=用作 GPIO，1=用作功能 IO（复位默认 0xFF，§3.2 行 156） |
| `GPIOADRV` | 0x618 | 428 | 8b | `DRV[7:0]` | 输出驱动选择：**0=8 mA，1=32 mA**（复位默认 0x0，§3.2 行 157） |
| `GPIOAPU` | 0x61C | 429 | 8b | `PU[7:0]` | 10 KΩ 上拉控制（输入模式有效，§3.2 行 149） |
| `GPIOAPD` | 0x620 | 430 | 8b | `PD[7:0]` | 10 KΩ 下拉控制（§3.2 行 150） |
| `GPIOAPU200K` | 0x624 | 431 | 8b | `PU200K[7:0]` | 200 KΩ 上拉控制（§3.2 行 151） |
| `GPIOAPD200K` | 0x628 | 432 | 8b | `PD200K[7:0]` | 200 KΩ 下拉控制（§3.2 行 152） |
| `GPIOAPU300` | 0x62C | 433 | 8b | `PU300[7:0]` | 300 Ω 上拉控制（§3.2 行 153） |
| `GPIOAPD300` | 0x630 | 434 | 8b | `PD300[7:0]` | 300 Ω 下拉控制（§3.2 行 154） |

#### GPIOB 寄存器组（sfr.h 行 436–448，本工程实际使用）

| 寄存器名 | 地址 | sfr.h 行号 | 关键位域 | 含义 |
|---|---|---|---|---|
| `GPIOBSET` | 0x640 | 436 | `SET[5:0]`（PB0–PB5） | 写 1 → 对应位输出高 |
| `GPIOBCLR` | 0x644 | 437 | `CLR[5:0]` | 写 1 → 对应位输出低 |
| `GPIOB` | 0x648 | 438 | `DAT[5:0]` | 读输入 / 写输出 |
| `GPIOBDIR` | 0x64C | 439 | `DIR[5:0]` | 0 输出，1 输入（复位 0x3F） |
| `GPIOBDE` | 0x650 | 440 | `DE[5:0]` | 数字 IO（复位 0x3F） |
| `GPIOBFEN` | 0x654 | 441 | `FEN[5:0]` | 功能映射使能（复位 0x3F） |
| `GPIOBDRV` | 0x658 | 442 | `DRV[5:0]` | 0=8 mA / 1=32 mA |
| `GPIOBPU` | 0x65C | 443 | `PU[5:0]` | 10 KΩ 上拉 |
| `GPIOBPD` | 0x660 | 444 | `PD[5:0]` | 10 KΩ 下拉 |
| `GPIOBPU200K` | 0x664 | 445 | `PU200K[5:0]` | 200 KΩ 上拉 |
| `GPIOBPD200K` | 0x668 | 446 | `PD200K[5:0]` | 200 KΩ 下拉 |
| `GPIOBPU300` | 0x66C | 447 | `PU300[5:0]` | 300 Ω 上拉 |
| `GPIOBPD300` | 0x670 | 448 | `PD300[5:0]` | 300 Ω 下拉 |

> **实测约束**：`main.c` `uart0_mapping_sel()` 中实际写的 PB 寄存器为 PB3（`|=BIT(3)`、`|=BIT(3)`），对应 `GPIOBSET/CLR/GPIOB/GPIOBDIR/GPIOBDE/GPIOBFEN/GPIOBPU` 共 7 个寄存器。

#### GPIOE 寄存器组（sfr.h 行 450–462，本工程实际使用）

| 寄存器名 | 地址 | sfr.h 行号 | 关键位域 | 含义 |
|---|---|---|---|---|
| `GPIOESET` | 0x680 | 450 | `SET[7:0]`（PE0/4/5/6/7） | 写 1 → 对应位输出高 |
| `GPIOECLR` | 0x684 | 451 | `CLR[7:0]` | 写 1 → 对应位输出低 |
| `GPIOE` | 0x688 | 452 | `DAT[7:0]` | 读输入 / 写输出 |
| `GPIOEDIR` | 0x68C | 453 | `DIR[7:0]` | 0 输出，1 输入（复位 0xFF） |
| `GPIOEDE` | 0x690 | 454 | `DE[7:0]` | 数字 IO（复位 0xFF） |
| `GPIOEFEN` | 0x694 | 455 | `FEN[7:0]` | 功能映射使能（复位 0xFF） |
| `GPIOEDRV` | 0x698 | 456 | `DRV[7:0]` | 0=8 mA / 1=32 mA |
| `GPIOEPU` | 0x69C | 457 | `PU[7:0]` | 10 KΩ 上拉 |
| `GPIOEPD` | 0x6A0 | 458 | `PD[7:0]` | 10 KΩ 下拉 |
| `GPIOEPU200K` | 0x6A4 | 459 | `PU200K[7:0]` | 200 KΩ 上拉 |
| `GPIOEPD200K` | 0x6A8 | 460 | `PD200K[7:0]` | 200 KΩ 下拉 |
| `GPIOEPU300` | 0x6AC | 461 | `PU300[7:0]` | 300 Ω 上拉 |
| `GPIOEPD300` | 0x6B0 | 462 | `PD300[7:0]` | 300 Ω 下拉 |

> **手册 §4.3 行 121–129 提示**：PE 端口物理上只存在 PE0 / PE4 / PE5 / PE6 / PE7，PE1/PE2/PE3 在本芯片上没有 PAD；任何对 `BIT(1/2/3)` 的操作都是无效位（保留 RO 0）。

#### GPIOF / GPIOG 寄存器组（sfr.h 行 464–491，本工程未使用）

| 寄存器组 | 地址范围 | sfr.h 行号 | 用途（手册 §3.2） |
|---|---|---|---|
| **GPIOF** | 0x6C0–0x6F0 | 464–476 | 与上面结构完全一致的 13 寄存器组。本工程未使用，但 F1/F5 可用作 `UART0-G6/G7 TX`（`include.h` 行 22–24）。 |
| **GPIOG** | 0x700–0x730 | 479–491 | PG1/PG2/PG4/PG5 默认是 MCP/SPI-Flash 复用引脚（`pinfunction.md` §4.5 行 142–152），**不可随意占用**。 |

### 2.2 端口功能映射寄存器（手册 §3.3 行 161–193）

| 寄存器名 | 地址 | sfr.h 行号 | 位域 | 默认 | 含义 |
|---|---|---|---|---|---|
| `FUNCMCON0` | 0x01C | 44 | `[3:0]` SD0MAP / `[7:4]` SPI0MAP / `[11:8]` UT0TXMAP / `[15:12]` UT0RXMAP / `[19:16]` HSTXMAP / `[23:20]` HSRXMAP / `[27:24]` UT1TXMAP / `[31:28]` UT1RXMAP | 0x0 | 写 `0xF/0xF` 到 UT0 位域可"清除映射"；本工程用 `UT0RXMAP=7`+"由 UT0TXMAP 选 TX 引脚"特性，把 PB3 当 TX/RX 单线 |
| `FUNCMCON1` | 0x020 | 45 | `[3:0]` FMOSCMAP / `[7:4]` UT2TXMAP / `[11:8]` UT2RXMAP / `[15:12]` SPI1MAP | 0x0 | UART2 映射：UT2TXMAP=2 → PB2(TX2-G2)，UT2RXMAP=2 → PB1(RX2-G2) |
| `FUNCMCON2` | 0x024 | 46 | `[3:0]` IISMAP / `[7:4]` T3CAPMAP / `[11:8]` T3PWMMAP / `[15:12]` T4PWMMAP / `[19:16]` T5PWMMAP / `[23:20]` IRMAP / `[27:24]` IICMAP / `[31:28]` DVPMAP | 0x0 | PWM / 捕获 / IIC / DVP 映射 |
| `FUNCMCON3` | 0x0FC | 102 | `[3:0]` PDMMAP / `[7:4]` MPDMMAP | 0x0 | PDM 数字麦克风映射 |

### 2.3 端口中断 / 唤醒寄存器（手册 §3.4 行 195–243）

| 寄存器名 | 地址 | sfr.h 行号 | 用途 |
|---|---|---|---|
| `WKUPCON` | 0xF24 | 414 | 唤醒中断使能 + 8 路唤醒输入使能（§3.4 行 212–217） |
| `WKUPEDG` | 0xF28 | 415 | 唤醒边沿选择（0 上升 / 1 下降）+ WKPND 状态（§3.4 行 219–224） |
| `WKUPCPND` | 0xF2C | 416 | 写 1 清唤醒挂起（§3.4 行 226–230） |
| `PORTINTEN` | 0xC7C | 577 | 32 路端口中断使能（§3.4 行 232–236） |
| `PORTINTEDG` | 0xC78 | 576 | 端口中断边沿选择（§3.4 行 238–242） |

> 本工程 `test_gpio.c` **不开启任何 GPIO 中断 / 唤醒**，纯轮询翻转；本节仅作为后续扩展 GPIO IRQ 的寄存器清单。

### 2.4 时钟门控与 GPIO 的间接关系

GPIO 控制器挂在系统主时钟域，**没有独立的 CLKGATE 寄存器**（手册 §3 未列出 `CLKGATx` 控制位）。与之相关的全局时钟在 `main.c` 行 232–233 切到 `x26m_div_clk = 1 MHz`（影响 Timer2 计时精度，进而影响 `delay_ms(500)` 的 1 Hz 周期准确度）。

---

## 3. 引脚定义与复用

> 数据来源：`docs/bt892x_pinfunction.md` §4.2 / §4.3（行 110–129）+ 附录 A 行 327–335。

### 3.1 本工程实际用到的 GPIO 引脚全表

| PAD | 端口组 | sfr.h 寄存器 | pinfunction.md 行号 | 物理可达性 | 复用 Group（本工程场景） |
|---|---|---|---|---|---|
| **PE0** | GPIOE | 行 450–462 | 125 | **跳过**：`高压 PIN MUTE / TYPE4`，仅 10K 固定上下拉，与高压电路耦合（手册 §4.3 备注 329） |
| **PE4** | GPIOE | 同上 | 126 | 可测 | `SPI0DO-G3` / `PWM1-T3-G4` / `DVP_PCLK_IN`；本测试 `FEN=0` 当纯 GPIO |
| **PE5** | GPIOE | 同上 | 127 | 可测 | `SDCMD-G3` / `SPI1DI-G4` / `IISSCLK-G2` / `IIC_DAT-G6` / `PWM0-T4-G1` / `FMOSC-G5` / `TMR3CAP_G6/IR_G6`；本测试当 GPIO |
| **PE6** | GPIOE | 同上 | 128 | 可测 | `SDCLK-G3` / `RX0-G4` / `SPI1CLK-G4` / `HSTRX-G9` / `PWM1-T4-G1` / `IISLRCLK-G2` / `IIC_CLK-G5/G6` / `DVP_VSYNC` / `TMR3CAP_G7/IR_G7`；本测试当 GPIO |
| **PE7** | GPIOE | 同上 | 129 | 可测 | `SDDAT0-G3` / `SPI1DO-G4` / `TX0-G4` / `HSTRX-G4` / `PWM2-T4-G1` / `IISDO-G2` / `IIC_DAT-G5` / `TMR4CAP_G1/IR_G8`；本测试当 GPIO |
| **PB0** | GPIOB | 行 436–448 | 114 | **跳过**：`WK1 唤醒源`，本测试不测 |
| **PB1** | GPIOB | 同上 | 115 | 可测 | `WK2 唤醒源` / `ADC3` / `AUXL1` / `RX0-G2` / `RX2-G2` / `SDCLK-G2` / `SPI1CLK-G3` / `IISMCLK-G3` / `IIC_CLK-G3/G4` / `HSTRX-G7` / `PWM1-T3-G1`；本测试当 GPIO，不进 sleep |
| **PB2** | GPIOB | 同上 | 116 | 可测 | `WK3 唤醒源` / `ADC4` / `AUXR1` / `TX0-G2` / `TX2-G2` / `SDDAT0-G2` / `SPI1DO-G3` / `IISSCLK-G3` / `IIC_DAT-G3` / `HSTRX-G2` / `PWM2-T3-G1`；本测试当 GPIO，不进 sleep |
| **PB3** | GPIOB | 同上 | 117 | **跳过**：`UART0 debug TX`（本工程 `main.c` printf 重定向到这里，详见 §5） |
| **PB4** | GPIOB | 同上 | 118 | **跳过**：`USBDM` USB 数据线，必须保留为 USB 模式（`pinfunction.md` 备注 333） |
| **PB5** | GPIOB | 同上 | 119 | **跳过**：`WKO 10S Reset 主唤醒源`（`pinfunction.md` 备注 332 + 334） |
| **PA7** | GPIOA | 行 422–434 | 108 | 被 `main.c uart0_mapping_sel()` 关闭（清除 UART0 G1 映射） |

### 3.2 FUNCMCONx 映射值汇总（手册 §3.3 行 161–193）

| 映射外设 | FUNCMCONx 位域 | 写入值（本工程涉及） | 实际效果 |
|---|---|---|---|
| **UART0 TX** | `FUNCMCON0[11:8] UT0TXMAP` | `3` (G3) → PB3 | 选 G3 = PB3 当 UART0 TX |
| **UART0 RX** | `FUNCMCON0[15:12] UT0RXMAP` | `7` (由 UT0TXMAP 选) | **单线模式**（手册 §3.3 行 167："0111=由 UT0TXMAP 选择 TX 引脚"），RX 与 TX 共用 PB3 |
| **UART0（关闭 G1）** | `FUNCMCON0[15:12]/[11:8]` | `(0xF<<12)\|(0xF<<8)` | 清除默认的 PA7 调试口（`main.c` 行 172） |
| **UART1 / UART2 / SPI0/1 / SD0 / IIS** | `FUNCMCON0/1/2/3` 各域 | `0xFF...` 或保留 | 本工程关闭：`main.c` 行 228–229 写 `FUNCMCON0 = 0xff000000`、`FUNCMCON1 = 0xffffffff`，等于把所有 UT1/UT2/SPI0/SD0 映射清除 |

### 3.3 引脚冲突约束（pinfunction.md 附录 A 行 327–335）

| 约束 | 来源 | 本工程处理 |
|---|---|---|
| **PE0 不可用**：TYPE4 / 高压 MUTE | pinfunction.md 行 329 | `test_gpio.c` 注释跳过（行 53） |
| **PG 默认被 MCP/SPI-Flash 占用** | pinfunction.md 行 334 + 142–152 | 本工程不碰 PG |
| **PB5 = WKO 10S Reset 主唤醒源** | pinfunction.md 行 332 | 不测 |
| **PB3 = USBDP / UART0 debug TX** | pinfunction.md 行 117 + 333 | 作为 printf 口保留，不测 |
| **PB4 = USBDM** | pinfunction.md 行 118 + 333 | USB 升级接口保留，不测 |
| **PB1/PB2 兼 WK2/WK3 唤醒源** | pinfunction.md 行 115–116 + 332 | 本测试不进 sleep，无冲突 |
| **同一 Group 内被多个外设共用** | pinfunction.md 行 331 + 332 | 例：`UT2TXMAP=2` 与 `UT0TXMAP=2` 都指向 PB2，但 `UART0-G2`（PB2）和 `UART2-G2`（PB2）是同一 PAD 的两个候选角色，**同时只能选一个** |

---

## 4. 初始化原理（为什么这样配）

### 4.1 时钟源

GPIO 控制器在系统主时钟域，无独立 CLKGATE。本工程在 `main.c` 行 232–233 把 `tmr_inc` 切到 `x26m_div_clk = 1 MHz`，配合 `TMR2CON`（`main.c` 行 102–106）得到 1 µs/tick 的 `delay_ms()`。这对 GPIO 翻转的**绝对周期准确度**至关重要——1 Hz 方波需要 500 ms + 500 ms = 1.000 s；如果 TMR2 时钟源仍是 26 MHz，`delay_ms()` 会把"毫秒"误算成"26 µs × 1000"，周期缩短到 26 ms（≈ 38.5 Hz），肉眼看似方波但逻辑分析仪立刻露馅。

### 4.2 方向（DIR）

`GPIOxDIR[pin] = 0` → **输出**（手册 §3.2 行 148）。复位默认 `DIR = 0xFF`（全输入），是为了上电不强行驱动外部线路造成短路。`test_gpio.c` 写 `GPIOxDIR &= ~BIT(pin)` 是"先读后改单 bit"的标准位操作模式。

### 4.3 数字使能（DE）

`GPIOxDE[pin] = 1` → 数字 IO（手册 §3.2 行 155）。复位默认 `DE = 0xFF`（全数字），看似不需要配置，但 `test_gpio.c` 仍写 `|= BIT(pin)` 是 **防御性编程**——某些低功耗模式下 PMU 可能关掉部分 PAD 的数字缓冲，重新切回 GPIO 时显式 `DE=1` 是稳妥做法。注意：**测试结束"恢复"段写 `DE &= ~BIT(pin)` 把该 PAD 切回模拟 IO**——这与手册复位默认（DE=0xFF=全数字）**不一致**，详见 §8 审查小节。

### 4.4 功能映射（FEN）

`GPIOxFEN[pin] = 0` → 用作 GPIO（手册 §3.2 行 156）。复位默认 `FEN = 0xFF`（全功能映射），意味着上电后所有 PAD 默认走 `FUNCMCONx` 选定的外设功能。`test_gpio.c` 写 `FEN &= ~BIT(pin)` 是把该 PAD 从"外设功能"切回"GPIO"的**唯一正确写法**——只清 `FEN` 不动 `FUNCMCONx`，让硬件临时放弃该 PAD 的外设角色即可，不需要再去操作 `FUNCMCONx` 的 0xF 清除流程。

### 4.5 输出置位 / 清除（SET / CLR）

手册 §3.2 行 146–147：**SET 写 1 置位 / CLR 写 1 清除，写 0 无动作**。这是**原子读写**，不需要"读-改-写"序列，**完全避免中断竞争**：

- `GPIOxSET = BIT(pin)`：高 1 个 PAD，其他位不动（不受中断影响）
- `GPIOxCLR = BIT(pin)`：低 1 个 PAD，其他位不动

如果改成 `GPIOx |= BIT(pin)` 这种"读-改-写"，在 ISR 同时改同一端口时会丢失一次翻转。**test_gpio.c** 的循环里全程用 SET/CLR（行 31/33）正是这个原因。

### 4.6 数据寄存器（DAT = GPIOx）

手册 §3.2 行 145：`GPIOx` **读**返回输入电平（与 PAD 同步采样），**写**直接驱动输出。**不建议**在多任务 / 中断环境用 `GPIOx = BIT(pin)` 翻转电平，会出现和上面一样的竞争。但本工程 `test_gpio.c` 末尾"恢复默认"段没有写 DAT，**没有产生竞争风险**。

### 4.7 上下拉 / 驱动（PU / PD / DRV）

`test_gpio.c` 完全没碰 PU/PD/DRV 寄存器——使用复位默认：PU/PD=0（无上下拉，靠 LA 探头高阻观察），DRV=0（8 mA，对 1 Hz 方波足够）。**注意**：PE4/PE5/PE6/PE7/PB1/PB2 浮空输出在上电瞬间电平不确定，**但测试只关心 1 Hz 翻转过程的稳态**，浮空不会导致观测错误（LA 输入阻抗典型 1 MΩ+，远大于 8 mA 驱动的等效阻抗）。

### 4.8 复用映射的因果链

本工程有两段 GPIO 操作都涉及 `FUNCMCON0`：

1. **`main.c` 行 168–179（`uart0_mapping_sel()`）**：先清掉默认的 PA7 调试口（`FUNCMCON0 = (0xf<<12)|(0xf<<8)`），再把 PB3 设成 TX/RX 单线（`FUNCMCON0 = (7<<12)|(3<<8)`）。PA7 切回普通 GPIO 输入，PB3 进入功能映射模式。
2. **`main.c` 行 228–229（`main` 入口）**：`FUNCMCON0 = 0xff000000`（关闭 UART1）、`FUNCMCON1 = 0xffffffff`（关闭 UART2/SPI1/FMOSC/IIS 等所有映射）。这两句是**保险措施**——确保即使前一段映射配置出错，也会被这里"全覆盖"为默认值（0xF）。

---

## 5. 初始化操作步骤

### 5.1 `uart0_mapping_sel()` 步骤（main.c 行 165–180）

| Step | 操作 | 寄存器 | 目的 |
|---|---|---|---|
| 1 | 关闭 PA7 上拉 | `GPIOAPU &= ~BIT(7)`（sfr.h 行 429） | 避免 PA7 上拉影响后续作 GPIO |
| 2 | PA7 切回 GPIO | `GPIOAFEN &= ~BIT(7)`（行 427） | 退出 UART0-G1 功能映射 |
| 3 | PA7 切为输入 | `GPIOADIR \|= BIT(7)`（行 425） | 让 PA7 不主动驱动外部 |
| 4 | PA7 切为模拟 | `GPIOADE &= ~BIT(7)`（行 426） | 关闭数字 IO，节省漏电 |
| 5 | 清除 UART0 映射 | `FUNCMCON0 = (0xf<<12)\|(0xf<<8)`（行 44） | UT0TXMAP=UT0RXMAP=0xF = 清除 |
| 6 | PB3 使能数字 IO | `GPIOBDE \|= BIT(3)`（行 440） | 进入数字模式 |
| 7 | PB3 接上拉 | `GPIOBPU \|= BIT(3)`（行 443） | 防止 TX 空闲时浮空 |
| 8 | PB3 切为输入（**注**） | `GPIOBDIR \|= BIT(3)`（行 439） | 准备单线收发（硬件自动切换方向） |
| 9 | PB3 进功能映射 | `GPIOBFEN \|= BIT(3)`（行 441） | 把 PB3 交给 FUNCMCON0 选定的 UART0 |
| 10 | 映射 UART0 到 PB3 单线 | `FUNCMCON0 = (7<<12)\|(3<<8)`（行 44） | UT0TXMAP=3(G3) → PB3 TX；UT0RXMAP=7 → RX=同一引脚（单线） |

> **Step 8 的疑点**：把 DIR 设成"输入"是为了让 PB3 进入单线模式（手册 §6.2 `ONELINE` 位），但在单线模式下硬件会自动切换 TX/RX 方向，**DIR 是输入**并不矛盾。详见 §8 审查。

### 5.2 `test_gpio.c` 中 `TEST_PIN_TOGGLE` 步骤（test_gpio.c 行 20–41）

| Step | 操作 | 寄存器 | 目的 |
|---|---|---|---|
| 1 | 数字 IO 使能 | `GPIOxDE \|= BIT(pin)`（行 21） | 让该 PAD 进入数字模式（默认已是 0xFF，防御性再写） |
| 2 | 退出功能映射 | `GPIOxFEN &= ~BIT(pin)`（行 22） | 让该 PAD 从外设功能回到 GPIO |
| 3 | 设为输出 | `GPIOxDIR &= ~BIT(pin)`（行 23） | DIR=0 = 输出 |
| 4 | 初始拉低 | `GPIOxCLR = BIT(pin)`（行 24） | 进入循环前先保证低电平起点 |
| 5 | **循环翻转 1Hz × 2s** | `SET = BIT(pin)` → `delay_ms(500)` → `CLR = BIT(pin)` → `delay_ms(500)`（行 31–34） | 产生 1Hz 方波；用 TMR2 1µs tick 计时 |
| 6 | "恢复"：设为输入 | `GPIOxDIR \|= BIT(pin)`（行 38） | DIR=1 = 输入 |
| 7 | "恢复"：进功能映射 | `GPIOxFEN \|= BIT(pin)`（行 39） | 让该 PAD 回到外设功能 |
| 8 | "恢复"：切模拟 IO | `GPIOxDE &= ~BIT(pin)`（行 40） | DE=0 = 模拟 IO |

> **疑点**：步骤 8 与手册复位默认（DE=0xFF=全数字）**不一致**。详见 §8 审查小节。

### 5.3 `main()` 入口的 GPIO 初始化顺序（main.c 行 222–250）

```
sd_disable();        // 关 SD0（行 226）：FUNCMCON0 SD0MAP=0xF，CLKGAT0 &=~BIT(9)
usb_disable();       // 关 USB（行 225）：USBCON0/1/2/3 清零，CLKGAT0 &=~BIT(14)
LVDCON &= ~BIT(30);  // 关 LVD（行 227）：低电压检测关
FUNCMCON0 = 0xff000000;  // 清 UART1 映射（行 228）
FUNCMCON1 = 0xffffffff;  // 清 UART2/SPI1/IIS/IIC 等所有映射（行 229）
timer2_init();       // TMR2 起 1µs tick（行 234）
uart0_mapping_sel(); // 配置 PB3 = UART0 单线（行 238）
UART0BAUD = ...;     // 设 1.5 Mbps 波特率（行 239）
my_printf_init(uart_putchar);  // printf 重定向到 UART0（行 250）
```

---

## 6. 代码详解

### 6.1 `main.c` 第 165–180 行 `uart0_mapping_sel()`

```c
165  void uart0_mapping_sel(void)
166  {
167      //close UART0 default PA7 print
168      GPIOAPU  &= ~BIT(7);                              // 关闭 PA7 上拉（避免残留上拉）
169      GPIOAFEN &= ~BIT(7);                              // PA7 切回 GPIO（退出 UART0-G1 映射）
170      GPIOADIR |= BIT(7);                               // PA7 切为输入（不驱动外部）
171      GPIOADE  &= ~BIT(7);                              // PA7 切模拟 IO（关数字缓冲，节电）
172      FUNCMCON0 = (0xf << 12) | (0xf << 8);             // UT0RXMAP=UT0TXMAP=0xF = 清除 UART0 映射
173  
174      //USB PB3 print
175      GPIOBDE  |= BIT(3);                               // PB3 进数字模式
176      GPIOBPU  |= BIT(3);                               // PB3 接 10K 上拉（TX 空闲时拉高）
177      GPIOBDIR |= BIT(3);                               // PB3 切为输入（单线模式硬件自动切方向）
178      GPIOBFEN |= BIT(3);                               // PB3 进功能映射（交给 FUNCMCON0 选 UART0）
179      FUNCMCON0 = (7 << 12) | (3 << 8);                 // UT0TXMAP=3(G3=PB3 TX), UT0RXMAP=7(RX=同 TX 引脚, 单线)
180  }
```

**逐条解释**：

- **168–171 行**：PA7 上电默认是 `TX0-G1(RX)`（pinfunction.md 行 108）。要彻底关闭这条映射需要：(a) 关闭上拉/数字 IO/方向 让 PA7 变高阻输入；(b) `FUNCMCON0[15:12]/[11:8]` 写 0xF 强制清除映射。
- **175–178 行**：PB3 上电默认是 `USBDP` / `SPI0DO-G3` / `TX0-G3(RX)`（pinfunction.md 行 117）。这里我们要把 PB3 **临时借给** UART0 单线：先开数字 IO、上拉、DIR=输入、然后 `FEN=1` 进功能映射，**最后**由 `FUNCMCON0` 选择 UART0 G3。注意 `DE=1 / DIR=输入 / FEN=1` 的组合：硬件在 `FEN=1` 时强制把 PAD 交给外设，DIR 不影响输出（外设驱动）。
- **179 行**：`UT0RXMAP=7` 的特殊含义（手册 §3.3 行 167）："由 UT0TXMAP 选择 TX 引脚"。即 **RX 复用 TX 的同一根引脚**，形成单线 UART（手册 §6.2 行 413 `ONELINE` 位类似效果，但本寄存器层面是 RXMAP=7 触发）。

### 6.2 `main.c` 第 225–250 行 `main()` 入口

```c
225      usb_disable();                       // 关 USB：USBCON0/1/2/3 = 0, CLKGAT0 &=~BIT(14)
226      sd_disable();                        // 关 SD0：SD0CON=0, CLKGAT0 &=~BIT(9), FUNCMCON0 SD0MAP=0xF
227      LVDCON &= ~BIT(30);                  // 关 LVD
228      FUNCMCON0 = 0xff000000;              // 强制清掉 UT1 TX/RX 映射
229      FUNCMCON1 = 0xffffffff;              // 强制清掉 UART2/SPI1/IIS/IIC/FMOSC 等所有映射
230      CLKCON2 &= 0x00ffffff;
231      CLKCON2 |= (25 << 24);               // x26m_div_clk = 1MHz（供 Timer2 用）
232      CLKCON0 &= ~(7 << 23);
233      CLKCON0 |= BIT(24);                  // tmr_inc select x26m_div_clk = 1M
234      timer2_init();                       // TMR2 起 1µs tick（DELAY_MS = 1000 ticks）
235      PWRCON0 |= BIT(20);                  // PMU normal
236      RTCCON3 |= BIT(0);                   // VDDBT enable（数字 LDO 1.2V）
237  
238      uart0_mapping_sel();                 // PB3 = UART0 单线
239      UART0BAUD = (UART_BAUD_VAL << 16) | UART_BAUD_VAL;  // 1.5 Mbps
240      memset(&__bss_start, 0, (u32)&__bss_size);  // 清 BSS
241      set_sys_clk(SYS_CLK);                // 切到 SYS_24M（默认）
242      timer0_init();                       // Timer0 初始化（10ms tick，喂看门狗）
...
250      my_printf_init(uart_putchar);        // printf 走 UART0 (PB3)
251  
252      printf("Hello SMART Flash MiniProj\n");
```

**GPIO 相关要点**：

- **228–229 行**：双保险。即便 `sd_disable()` 已经清了 SD0 映射、`uart0_mapping_sel()` 已经清了 UART0 G1 映射，这里再次把 FUNCMCON0/1 高位全部置 1（=清除）确保没有任何残留外设映射干扰后续 GPIO 测试。
- **231–233 行**：`x26m_div_clk = 1 MHz` 是 `delay_ms()` 的时间基准。如果忘了这一步，TMR2 还是 26 MHz，`delay_ms(500)` 实际只有 19.2 ms，1 Hz 周期就变成 38.5 Hz。
- **238 行**：调用 `uart0_mapping_sel()`，把 PB3 切到 UART0 单线模式。

### 6.3 `test_common.h` GPIO 宏（test_common.h 行 14–35）

```c
14  #define TEST_GPIO_OUT_HELPER(PORT, PIN)  do { \
15          GPIO##PORT##DIR &= ~BIT(PIN); \    // DIR=0 输出
16          GPIO##PORT##DE  |=  BIT(PIN); \    // DE=1 数字 IO
17          GPIO##PORT##FEN &= ~BIT(PIN); \    // FEN=0 用作 GPIO
18      } while (0)
19  #define TEST_GPIO_OUT(PORT, PIN)  TEST_GPIO_OUT_HELPER(PORT, PIN)   // 双层宏：先展开 PORT
20  
21  #define TEST_GPIO_IN_HELPER(PORT, PIN)   do { \
22          GPIO##PORT##DIR |=  BIT(PIN); \    // DIR=1 输入
23          GPIO##PORT##DE  |=  BIT(PIN); \    // DE=1 数字 IO
24          GPIO##PORT##FEN &= ~BIT(PIN); \    // FEN=0 用作 GPIO
25      } while (0)
...
28  #define TEST_GPIO_HIGH_HELPER(PORT, PIN)  (GPIO##PORT##SET = BIT(PIN))   // 置位 = 高
29  #define TEST_GPIO_LOW_HELPER(PORT, PIN)   (GPIO##PORT##CLR = BIT(PIN))   // 清除 = 低
34  #define TEST_GPIO_READ_HELPER(PORT, PIN)  ((GPIO##PORT >> (PIN)) & 1u)   // 读输入电平
```

**关键技术点（test_common.h 行 10–11 注释）**：

> "C 标准规定，宏参数在使用 `##` 拼接时**不会**先被展开。因此需要两层宏：外层先展开 PORT 参数，内层再做 `##` 拼接。"

也就是说：`GPIO##PORT##DE` 中 `PORT` 是宏形参，`##` 拼接时直接当字面字符，所以 `TEST_GPIO_OUT(E, 4)` 第一层展开为 `TEST_GPIO_OUT_HELPER(E, 4)`，第二层 `PORT=E` 替换为 `GPIOEDE` ——**这两层都是必要的**，写成单层宏就是 `GPIO##E##DE` 这种语法糖，C 预处理器无法处理。

### 6.4 `test_gpio.c` 自定义宏 `TEST_PIN_TOGGLE`（行 20–41）

```c
20  #define TEST_PIN_TOGGLE(PORT, pin_num, duration_ms) do { \
21          GPIO##PORT##DE   |=  BIT(pin_num); \    // DE=1 数字 IO
22          GPIO##PORT##FEN &= ~BIT(pin_num); \    // FEN=0 用作 GPIO
23          GPIO##PORT##DIR &= ~BIT(pin_num); \    // DIR=0 输出
24          GPIO##PORT##CLR  =   BIT(pin_num); \   // 初始拉低
25          TEST_LOG("========================================"); \
26          TEST_LOG(">>> Testing P" #PORT "%d: toggle 1Hz for %u ms", (u32)pin_num, duration_ms); \
27          TEST_LOG(">>> Probe P" #PORT "%d with logic analyzer now!", (u32)pin_num); \
28          u32 _t0 = TMR2CNT; \
29          u32 _cnt = 0; \
30          while ((u32)(TMR2CNT - _t0) < (duration_ms) * 1000) { \
31              GPIO##PORT##SET = BIT(pin_num); \   // 高 500ms
32              delay_ms(500); \
33              GPIO##PORT##CLR = BIT(pin_num); \   // 低 500ms
34              delay_ms(500); \
35              _cnt++; \
36          } \
37          TEST_LOG("<<< P" #PORT "%d done (%u toggles, pin left LOW)", (u32)pin_num, _cnt); \
38          GPIO##PORT##DIR |= BIT(pin_num); \    // "恢复"：DIR=1 输入
39          GPIO##PORT##FEN |= BIT(pin_num); \    // "恢复"：FEN=1 功能映射
40          GPIO##PORT##DE  &= ~BIT(pin_num); \   // "恢复"：DE=0 模拟 IO（与复位默认 DE=0xFF 不一致）
41      } while (0)
```

**逐条解释**：

- **21–24 行**：与 `TEST_GPIO_OUT_HELPER` 几乎一致，**唯一的差异是顺序**：先开 DE 再清 FEN 再设 DIR，最后 `CLR=1` 给一个确定的低电平起点（避免上电浮空导致逻辑分析仪抓到不定波形）。
- **25–27 行**：打印分隔线 + 提示"现在用逻辑分析仪夹探针"。这是**用户体验设计**——给操作员充足时间反应。
- **28–29 行**：用 `TMR2CNT`（SFR0_BASE+0x3C×4=0xF0，sfr.h 行 99）记录起点和翻转次数。
- **30–36 行**：`while ((u32)(TMR2CNT - _t0) < duration_ms * 1000)` 是 **u32 无符号减法** 利用 32-bit 自然回绕判断"经过了多少 tick"。这里 `duration_ms=2000`，`2000 * 1000 = 2,000,000 µs = 2 秒`，每 500 ms 翻转一次共 4 次翻沿 = 2 个完整周期。
- **38–40 行**：**注意**这三行被注释为"恢复"，但与手册复位默认有出入：

| 状态 | 复位默认 | 本宏"恢复"后 |
|---|---|---|
| DIR | 1（输入） | 1（输入）一致 |
| FEN | 1（功能映射） | 1（功能映射）一致 |
| **DE** | **1（数字 IO）** | **0（模拟 IO）不一致** |

这导致引脚**恢复后处于模拟模式**，如果后续代码又去操作该 PAD 的 `SET/CLR/DAT` 而没先 `DE=1`，会"操作无效"。详见 §8 审查。

### 6.5 `test_gpio.c` 主函数（行 43–85）

```c
43  void test_gpio_run(void)
44  {
45      TEST_LOG("========================================");
46      TEST_LOG("PE/PB GPIO diagnostic test");
47      TEST_LOG("Connect logic analyzer probes to PE4/5/6/7 and PB1/2");
48      TEST_LOG("Each pin will toggle 1Hz for 2 seconds");
49      TEST_LOG("========================================");
50  
51      // ===== PE 端口 =====
52      // PE1/PE2/PE3 不存在（手册 §4.3 只列出 PE0, PE4, PE5, PE6, PE7）
53      // PE0 为 MUTE PIN（高压相关），本测试跳过
54      TEST_LOG("");
55      TEST_LOG("*** PE port ***");
56  
57      // PE4
58      TEST_PIN_TOGGLE(E, 4, TOGGLE_PER_PIN_MS);   // 2 秒 1Hz 方波
59      // PE5
60      TEST_PIN_TOGGLE(E, 5, TOGGLE_PER_PIN_MS);
61      // PE6
62      TEST_PIN_TOGGLE(E, 6, TOGGLE_PER_PIN_MS);
63      // PE7
64      TEST_PIN_TOGGLE(E, 7, TOGGLE_PER_PIN_MS);
65  
66      // ===== PB 端口 =====
67      // 跳过 PB0 / PB3 (debug TX) / PB4 (USBDM) / PB5 (WKO reset)
68      TEST_LOG("");
69      TEST_LOG("*** PB port (only PB1/PB2) ***");
70  
71      // PB1 (WK2 wakeup source)
72      TEST_PIN_TOGGLE(B, 1, TOGGLE_PER_PIN_MS);
73      // PB2 (WK3 wakeup source)
74      TEST_PIN_TOGGLE(B, 2, TOGGLE_PER_PIN_MS);
75  
76      TEST_LOG("========================================");
77      TEST_LOG("All tested pins done.");
78      TEST_LOG("========================================");
79  
80      while (1);    // 死循环，停止后续 test_*_run 调用
81  }
```

**关键点**：

- **行 80**：`while (1)` 死循环，防止 `main.c` 行 271–273 在 GPIO 测试后继续跑到其他 `test_*_run()`（多个 `TEST_*_EN` 同时启用时，后跑的测试会破坏前面的测试状态）。
- **跳过 PE0/PE1/2/3**：PE0 是高压 MUTE（手册 §4.3 备注 329）；PE1/PE2/PE3 在本芯片上不存在 PAD（手册 §4.3 只列 PE0/PE4/5/6/7）。
- **跳过 PB0/PB3/PB4/PB5**：PB0 没在本轮目标；PB3 是 printf TX；PB4 是 USB DM；PB5 是 WKO 10S Reset 主唤醒源（pinfunction.md 备注 332–334）。

---

## 7. 测试步骤与预期现象

> 本节是 **可执行的实验手册**，所有步骤在 `docs/test_gpio.md` 已通过实测（PE4/PE5/PE6/PE7/PB1/PB2 1Hz 方波翻转均通过）。

### 7.1 接线方式

| 信号 | 位置 | 用途 |
|---|---|---|
| **逻辑分析仪 CH0** | PE4 | 抓 PE4 波形 |
| **逻辑分析仪 CH1** | PE5 | 抓 PE5 波形 |
| **逻辑分析仪 CH2** | PE6 | 抓 PE6 波形 |
| **逻辑分析仪 CH3** | PE7 | 抓 PE7 波形 |
| **逻辑分析仪 CH4** | PB1 | 抓 PB1 波形 |
| **逻辑分析仪 CH5** | PB2 | 抓 PB2 波形 |
| **USB-TTL 串口** | PB3 (TX) @ 1.5 Mbps 8N1 | 抓 [TEST] 日志 |
| **电源** | 3.3V/5V → BT892X 开发板 | 供电 |

> **GND 必须共地**：逻辑分析仪与 USB-TTL 串口的 GND 与开发板 GND 短接。

### 7.2 编译开关（在 main.c 行 50）

```c
// #define TEST_GPIO_EN    1     // 取消注释这一行启用 GPIO 测试
// 其它 #define TEST_*_EN 全部保持注释（一次只跑一个 test）
```

### 7.3 操作步骤

1. 用 CodeBlocks 打开 `app.cbp`，编译。
2. 把 `app.bin` 烧录到 BT892X 开发板。
3. **先**把 USB-TTL 串口接到 PB3 / GND，打开串口助手（1.5 Mbps 8N1）。
4. **再**把逻辑分析仪探针分别夹到 PE4 / PE5 / PE6 / PE7 / PB1 / PB2。
5. 给开发板上电 / 按 RESET。
6. 串口立即打印 `[TEST] ========================================`，进入循环测试。

### 7.4 串口输出预期（来自 test_gpio.md §2.5 实测）

```
[TEST] ========================================
[TEST] PE/PB GPIO diagnostic test
[TEST] Connect logic analyzer probes to PE4/5/6/7 and PB1/2
[TEST] Each pin will toggle 1Hz for 2 seconds
[TEST] ========================================
[TEST] 
[TEST] *** PE port ***
[TEST] >>> Testing PE4: toggle 1Hz for 2000 ms
[TEST] >>> Probe PE4 with logic analyzer now!
[TEST] <<< PE4 done (2 toggles, pin left LOW)
[TEST] >>> Testing PE5: toggle 1Hz for 2000 ms
[TEST] >>> Probe PE5 with logic analyzer now!
[TEST] <<< PE5 done (2 toggles, pin left LOW)
[TEST] >>> Testing PE6: toggle 1Hz for 2000 ms
[TEST] >>> Probe PE6 with logic analyzer now!
[TEST] <<< PE6 done (2 toggles, pin left LOW)
[TEST] >>> Testing PE7: toggle 1Hz for 2000 ms
[TEST] >>> Probe PE7 with logic analyzer now!
[TEST] <<< PE7 done (2 toggles, pin left LOW)
[TEST] 
[TEST] *** PB port (only PB1/PB2) ***
[TEST] >>> Testing PB1: toggle 1Hz for 2000 ms
[TEST] >>> Probe PB1 with logic analyzer now!
[TEST] <<< PB1 done (2 toggles, pin left LOW)
[TEST] >>> Testing PB2: toggle 1Hz for 2000 ms
[TEST] >>> Probe PB2 with logic analyzer now!
[TEST] <<< PB2 done (2 toggles, pin left LOW)
[TEST] ========================================
[TEST] All tested pins done.
[TEST] ========================================
```

**关键字段解读**：

- `2 toggles`：循环 `_cnt` 在 2 秒内累积 2 次（每次完整 500ms 高 + 500ms 低 + 重新进入 SET/CLR）。
- `pin left LOW`：循环最后一步是 `CLR = BIT(pin)`（行 33），退出 while 后 PAD 保持低电平。

### 7.5 逻辑分析仪波形预期

每个引脚独立观察 2 秒（约 2 个完整周期）：

| 引脚 | 预期波形 | 高电平 | 低电平 | 周期 | 占空比 |
|---|---|---|---|---|---|
| PE4 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |
| PE5 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |
| PE6 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |
| PE7 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |
| PB1 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |
| PB2 | 1 Hz 方波 | ≈ 3.3 V | ≈ 0 V | 1.000 s ± 1 ms | 50 % |

> **精度来源**：`delay_ms(500)` 实际是 `TICK_1MS * 500 = 1000 * 500 = 500 000 ticks`，TMR2 跑 1 µs/tick，所以理论精度 = 1 µs / 500 000 µs = 2 ppm。实测周期偏差主要来自 TMR2CNT 读取的额外开销（`while` 条件判断 + 取指），一般 < 0.1 %。

### 7.6 失败排查（test_gpio.md §4 已罗列）

| 现象 | 排查方向 |
|---|---|
| 编译 `undeclared GPIOxxxDIR` | 双层宏未生效（PORT 没先展开） |
| `undefined reference to test_gpio_run` | `app.cbp` 的 `<Unit>` 未登记 `test/test_gpio.c` |
| 引脚无波形 | PAD 物理不可达 / 被外设占用 → 检查 `FEN` 是否清零 |
| 引脚一直高 | DIR 配错 → 确认 `GPIOxDIR &= ~BIT(pin)` |
| 周期远大于 1 s | `set_sys_clk(SYS_24M)` 没调到 24 MHz |

---

## 8. 审查小节（对照手册与引脚定义）

### 8.1 一致性核对结论

| 项 | 手册依据 | 代码实际 | 一致性 |
|---|---|---|---|
| **DIR=0 输出，1 输入** | 手册 §3.2 行 148（复位默认 0xFF） | `GPIOxDIR &= ~BIT(pin)`（行 23 / 行 38） | ✅ 完全一致 |
| **DE=1 数字 IO** | 手册 §3.2 行 155（复位默认 0xFF） | `GPIOxDE \|= BIT(pin)`（行 21） | ✅ 完全一致 |
| **FEN=0 用作 GPIO，1 功能映射** | 手册 §3.2 行 156（复位默认 0xFF） | `GPIOxFEN &= ~BIT(pin)`（行 22） | ✅ 完全一致 |
| **SET 写 1 置位** | 手册 §3.2 行 146 | `GPIOxSET = BIT(pin)`（行 31） | ✅ 完全一致 |
| **CLR 写 1 清除** | 手册 §3.2 行 147 | `GPIOxCLR = BIT(pin)`（行 24 / 行 33） | ✅ 完全一致 |
| **DAT 读输入/写输出** | 手册 §3.2 行 145 | 未使用 `GPIOx = BIT(pin)` 写法（避免竞争） | ✅ 设计层面一致 |
| **PE 端口仅 PE0/4/5/6/7 有效** | 手册 §4.3 行 121–129 | 跳过 PE1/PE2/PE3（行 52） | ✅ 完全一致 |
| **PB3 = USBDP / UART0 debug TX** | pinfunction.md 行 117 + 333 | 跳过 PB3 作 GPIO 测试（行 67） | ✅ 完全一致 |
| **PB5 = WKO 10S Reset 主唤醒源** | pinfunction.md 行 332 | 跳过 PB5（行 67） | ✅ 完全一致 |
| **PE0 = 高压 MUTE PIN（TYPE4）** | pinfunction.md 行 125 + 329 | 跳过 PE0（行 53） | ✅ 完全一致 |
| **UART0 G3 = PB3 单线（RXMAP=7）** | 手册 §3.3 行 167 "0111=由 UT0TXMAP 选择" | `FUNCMCON0 = (7<<12)\|(3<<8)`（main.c 行 179） | ✅ 完全一致 |
| **清除映射写 0xF** | 手册 §3.3 行 167–168 "1111=清除" | `(0xf<<12)\|(0xf<<8)`（main.c 行 172） | ✅ 完全一致 |
| **PB1/PB2 兼 WK2/WK3 唤醒源但本测试不进 sleep** | pinfunction.md 行 115–116 | `TEST_PIN_TOGGLE(B,1,...)` / `(B,2,...)`（行 72 / 74） | ✅ 不进 sleep 无冲突 |

### 8.2 发现的疑点（**只记录，不要求改代码**）

#### 疑点 A：`test_gpio.c` 自定义宏 `TEST_PIN_TOGGLE` 未复用 `test_common.h` 的 `TEST_GPIO_*`（**代码重复**）

- 现象：test_gpio.c 行 20–41 自定义了 `TEST_PIN_TOGGLE`，但 `test_common.h` 行 14–19 已有 `TEST_GPIO_OUT_HELPER`，两者**功能几乎完全相同**（DE/FEN/DIR 配置都一样）。
- 唯一差异：`TEST_PIN_TOGGLE` 多了：
  1. `CLR = BIT(pin)` 初始拉低（行 24）
  2. 加了 `delay_ms(500) + SET/CLR` 翻转循环（行 30–36）
  3. 末尾"恢复"段（行 38–40）
- 建议（**仅供参考**）：后续可考虑拆出 `_HELPER_OUT_AND_LOW(PORT, PIN)` 子宏让两个宏共用，但 `TEST_PIN_TOGGLE` 的语义是"翻转一段时间再恢复"，与单纯的 `TEST_GPIO_OUT` 有本质区别，**当前结构清晰可读**。

#### 疑点 B：`TEST_PIN_TOGGLE` 末尾"恢复"段把 `DE` 清 0（**与手册复位默认不一致**）

- 现象：test_gpio.c 行 40 `GPIOxDE &= ~BIT(pin)` 把引脚切成模拟 IO。
- 手册 §3.2 行 155 复位默认：`DE = 0xFF`（**全数字 IO**）。本工程"恢复"后引脚处于**模拟 IO** 而非手册默认的**数字 IO**。
- 影响：
  1. 如果后续代码（比如进入下一个 `test_*_run`）对该 PAD 写 `SET/CLR/DAT`，硬件会忽略（DE=0），看起来像"操作无效"。
  2. 但 `test_gpio_run` 行 80 `while (1)` 死循环保证不会执行其他 test，**所以在本测试范围内没有实际危害**。
  3. 后续维护者如果删掉死循环或加新测试，会踩坑。
- 建议（**仅供参考**）：要么把行 40 改成 `DE |= BIT(pin)`（保持数字 IO），要么明确写注释说明"本测试结束后 PAD 进入模拟 IO 是有意为之（节电）"。

#### 疑点 C：`uart0_mapping_sel()` 中 `GPIOBDIR |= BIT(3)`（行 177）的语义模糊

- 现象：PB3 在被设为 UART0 单线之前先 `DIR=输入`。
- 手册 §6.2 行 413 `ONELINE` 位：本工程没有用 `ONELINE`，而是通过 `UT0RXMAP=7` + `UT0TXMAP=3` 实现单线。
- 解读 A：单线模式下，硬件会自动切 TX/RX 方向，**DIR 设为输入让硬件接管**。
- 解读 B：先 DIR=输入再 FEN=1 进功能映射，是为了让 FEN 切换瞬间 PAD 不主动驱动（避免线与冲突）。
- 当前代码对单线模式的具体硬件行为手册未详述，**无明确结论**，保留现状。

#### 疑点 D：恢复段 `FEN |= BIT(pin)` 与"恢复默认"语义不一致

- 现象：test_gpio.c 行 39 把 `FEN=1`（功能映射）。
- 手册 §3.2 行 156 复位默认：`FEN = 0xFF`（**全功能映射**），但**实际硬件上电会先走 `FUNCMCONx` 的映射**，所以"上电后默认状态"≠"用户态默认"。
- 影响：恢复后该 PAD 仍受 `FUNCMCONx` 影响，可能被映射到 SPI0/PWM 等（本工程已 `FUNCMCON0/1` 全清，所以**短期内无冲突**）。
- 与疑点 B 结合：恢复段把 DE=0、FEN=1，意味着该 PAD **模拟模式 + 功能映射**——这种组合手册没有典型用例，但也没有禁止。

#### 疑点 E：`delay_ms(500)` 期间 TMR2CNT 回绕风险

- 现象：test_gpio.c 行 30 `(TMR2CNT - _t0) < duration_ms * 1000` 用 u32 无符号减法。
- TMR2CNT 是 32-bit，1 µs/tick 计数到 0xFFFFFFFF 需要约 4295 秒（71 分钟）。`duration_ms=2000` 对应 2,000,000 µs 远小于 4295 秒，**本测试范围内安全**。
- 如果后续有人把 `duration_ms` 改成 `>4,000,000`（≈ 67 分钟），循环会**立即退出**。建议加 `u64` 或分片处理。**仅记录，不改**。

#### 疑点 F：FUNCMCONx 写后未读回校验

- 现象：main.c 行 228–229 直接 `FUNCMCON0 = 0xff000000` / `FUNCMCON1 = 0xffffffff`。
- 手册未规定这些寄存器的"写后必须读回确认"，但部分硬件设计有写缓冲。**未观察到异常**，可能 BT892X 硬件写穿（write-through）。**仅记录**。

---

**文档结束**。本说明基于以下真实文件（绝对路径）：

- `d:\Code\smart_mini\minimax\smart_mini\smart_mini\header\sfr.h`（GPIO A/B/E/F/G 寄存器定义，行 422–491）
- `d:\Code\smart_mini\minimax\smart_mini\smart_mini\test\test_gpio.c`（GPIO 测试，行 1–85）
- `d:\Code\smart_mini\minimax\smart_mini\smart_mini\test\test_common.h`（GPIO 宏，行 14–35）
- `d:\Code\smart_mini\minimax\smart_mini\smart_mini\main.c`（`uart0_mapping_sel`，行 165–180）
- `d:\Code\smart_mini\minimax\smart_mini\docs\BT892X_UserManual_Driver.md`（§3 GPIO，行 134–243）
- `d:\Code\smart_mini\minimax\smart_mini\docs\bt892x_pinfunction.md`（§4 通用 IO + 附录 A 注意事项，行 110–334）
- `d:\Code\smart_mini\minimax\smart_mini\docs\test_gpio.md`（已通过测试报告 §2.5 实测串口输出 + §4 失败排查）