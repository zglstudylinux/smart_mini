# BT892X ADKEY 外设说明文档

> **目标读者**：本工程维护者、后续驱动/应用开发者、教学培训
> **芯片**：中科蓝讯 BT892X（32-bit RISC-V SoC）
> **工程**：`d:\Code\smart_mini\minimax\smart_mini\smart_mini\app.cbp`（CodeBlocks，寄存器裸操作，无独立 driver 层）
> **关联外设**：SARADC（逐次逼近型 ADC，16 通道 10 位）、GPIO PORTB、TMR1、PIC
> **关联文档**：
> - `docs/SARADC_CTL (逐次逼近型 ADC) 数据手册摘要.md`（手册第 11 章）
> - `docs/bt892x_pinfunction.md`（§4.2 PORTB、§8.10 SARADC 通道分配、附录 A 唤醒源）
> - `docs/BT892X_UserManual_Driver.md`（§3 GPIO、§4 定时器、§2 中断系统）
> - `docs/test_adkey.md`（已通过的五阶段测试报告与实测数据）

---

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 ADKEY 是什么

ADKEY（Analog Key）是低成本蓝牙音箱/耳机类 SoC 上常见的"一颗 GPIO 复用 ADC 通道 + 多按键分压电阻网络"实现方案。其本质：

- 物理层：1 个 SARADC 通道 + 一组并联到 GND 的"按键+串联电阻"分压网络。
- 协议层：分压节点的模拟电压经 ADC 量化后落到不同数字区间，区间映射到具体按键。
- 优势：仅占用 1 个 ADC 通道即可区分 N 个按键，布线与 GPIO 占用远低于独立按键矩阵。

本工程中 BT892X 板载 PWRKEY 网络由 3 个按键 + 3 个不同串联电阻组成，复用 SARADC 通道 **ADC12**（即 PB5）。通过对 ADC12 的原始值进行"区间判定 + 5×5ms 消抖 + 700ms 长按 + 200ms HOLD 连发"的状态机处理，向主循环抛出标准化的按键消息（SHORT/SHORT_UP/LONG/LONG_UP/HOLD）。

### 1.2 本工程用途

`test/test_adkey.c` 提供五个独立测试入口，对应阶段一～五。每一阶段在前一阶段基础上累加功能：

| 阶段 | 函数 | 入口宏（main.c） | 在累加链上的位置 |
|:---:|:---|:---|:---|
| 阶段一 | `test_adkey_raw_run` | `TEST_ADKEY_RAW_EN` | SARADC + GPIO 初始化、原始 ADC12 采样 |
| 阶段二 | `test_adkey_map_run` | `TEST_ADKEY_MAP_EN` | 在阶段一基础上加入"原始值→键值"区间映射 |
| 阶段三 | `test_adkey_debounce_run` | `TEST_ADKEY_DEBOUNCE_EN` | 在阶段二基础上加入 TMR1 5ms 扫描 + 5 次连续相同值的 25ms 消抖 |
| 阶段四 | `test_adkey_long_run` | `TEST_ADKEY_LONG_EN` | **已并入阶段五**，本入口当前仅打印引导日志并死循环（链接器空间优化，见测试报告 §12） |
| 阶段五 | `test_adkey_hold_run` | `TEST_ADKEY_HOLD_EN` | 在阶段三基础上加入 700ms 长按 + 200ms HOLD 连发 |

阶段五已通过；阶段四的 SHORT/LONG/LONG_UP 行为由阶段五的状态机完整复现。阶段四函数体被最小化仅作为引导。

### 1.3 涉及文件清单（相对路径，相对工程根 `smart_mini/smart_mini/`）

| 类别 | 相对路径 | 说明 |
|:---|:---|:---|
| 测试主体 | `test/test_adkey.c` | 五阶段测试实现，全部裸 SFR 操作 |
| 测试头 | `test/test_adkey.h` | 5 个 `void test_xxx_run(void)` 入口声明；键值/消息宏 |
| 共用宏 | `test/test_common.h` | `TEST_LOG(fmt,...)` 串口日志前缀；`TEST_GPIO_*` 输入/输出/读/写宏 |
| 寄存器映射 | `header/sfr.h` | SARADC、GPIO PORTB、CLKCON0/CLKGAT0、TMR1、PIC 等 SFR 地址定义 |
| 中断号定义 | `header/int.h` | `IRQ_TMR1_VECTOR = 4` |
| 主入口 | `main.c` | 第 73-77 行注释列出 5 个 `TEST_ADKEY_*_EN` 宏；第 327-341 行 `#ifdef` 分发到对应 `test_adkey_*_run()` |
| 参考文档 | `docs/SARADC_CTL (逐次逼近型 ADC) 数据手册摘要.md` | SARADC 寄存器 §11.2、使用指南 §11.3 |
| 参考文档 | `docs/bt892x_pinfunction.md` | §4.2 PORTB 引脚表、§8.10 通道→引脚分配、附录 A 注意事项 |
| 参考文档 | `docs/BT892X_UserManual_Driver.md` | §2 中断、§3 GPIO、§4 定时器 |
| 测试报告 | `docs/test_adkey.md` | 已通过的五阶段实测数据 |

---

## 2. 涉及寄存器逐个说明

> 行号与地址全部来自 `smart_mini/smart_mini/header/sfr.h` 真实读取结果。SFR 基地址 `SFRx_BASE` 由工具链头间接给出，本表只记录相对 SFR 组内的偏移。手册章节号引用自 `docs/SARADC_CTL (逐次逼近型 ADC) 数据手册摘要.md`（即 PDF 第 11 章）。

### 2.1 时钟与门控

| 寄存器 | sfr.h 行号 | 偏移（相对基址） | 属性 | 关键位域 | 含义（手册章节） |
|:---|:---:|:---|:---:|:---|:---|
| `CLKCON0` | 63 | `SFR0_BASE + 0x19*4` | RW | `[28]` = SARADC 时钟源选择 | `0` = `rc2m_clk`；`1` = `x24m_clkdiv4`（= 6MHz）。本工程置 1（test_adkey.c:54） |
| `CLKGAT0` | 282 | `SFR3_BASE + 0x2c*4` | RW | `[13]` = SARADC 时钟门 | `0` = 门关闭（无时钟）；`1` = 门打开。本工程置 1（test_adkey.c:55） |
| `TMR1CON` | 93 | `SFR0_BASE + 0x35*4` | RW | `[9]` TPND / `[7]` TIE / `[6]` INCSRC / `[3:2]` INCSEL / `[0]` TMREN | 32-bit 基础定时器控制。本工程使用 `INCSEL=00`（系统时钟）、`TIE=1`、`TMREN=1`，由 `tmr_inc = 1MHz` 驱动（test_adkey.c:217-227） |
| `TMR1CPND` | 94 | `SFR0_BASE + 0x36*4` | RW | `[9]` TPCLR（写 1 清挂起） | ISR 中 `TMR1CPND = BIT(9)` 清溢出挂起（test_adkey.c:211、223） |
| `TMR1CNT` | 95 | `SFR0_BASE + 0x37*4` | RW | `[31:0]` TMRCNT | 计数器递增；等于 PR 时溢出清零并置 TPND。本工程每次扫描前清零（test_adkey.c:224） |
| `TMR1PR` | 96 | `SFR0_BASE + 0x38*4` | RW | `[31:0]` TMRPR | 周期值。`TMR1PR = ADKEY_SCAN_PERIOD_US - 1 = 4999`，得到 5ms 周期（test_adkey.c:225） |

### 2.2 SARADC（手册第 11 章）

| 寄存器 | sfr.h 行号 | 偏移（相对基址） | 属性 | 关键位域 | 含义（手册章节） |
|:---|:---:|:---|:---:|:---|:---|
| `SADCCON` | 405 | `SFR5_BASE + 0x30*4` | RW | `[19] ADCAEN` | `1` = 自动使能 SARADC 模拟模块（手册 §11.2 寄存器 11-1） |
| `SADCCON` | 405 | 同上 | RW | `[18] ADCANGIO` | `1` = 自动使能模拟 IO（手册 §11.2 寄存器 11-1） |
| `SADCCON` | 405 | 同上 | RW | `[17] ADCIE` | `1` = SARADC 中断使能；本工程保持 `0`（轮询模式） |
| `SADCCON` | 405 | 同上 | RW | `[16] ADCEN` | `1` = SARADC 主模块使能（手册 §11.2 寄存器 11-1） |
| `SADCCON` | 405 | 同上 | RW | `[15:0] CHnPUEN` | 各通道内部 100kΩ 上拉使能；本工程置 `CH12PUEN=1`（手册 §11.2 寄存器 11-1） |
| `SADCCH` | 406 | `SFR5_BASE + 0x31*4` | RW | `[16] ADCPND` | 转换完成标志（只读语义，写 `SADCCH` 自动清零；手册 §11.2 寄存器 11-2） |
| `SADCCH` | 406 | 同上 | RW | `[15:0] CHnEN` | 通道使能；**写 1 启动该通道转换并清 ADCPND**（手册 §11.2 寄存器 11-2 / §11.3 步骤 4） |
| `SADCST` | 407 | `SFR5_BASE + 0x32*4` | WO | `[31:0]` 各通道 setup time | 通道建立时间：`00=0`、`01=2`、`10=4`、`11=8` 个 SARADC_CLK（手册 §11.2 寄存器 11-3）。本工程保持默认 `0`（test_adkey.c:72） |
| `SADCBAUD` | 408 | `SFR5_BASE + 0x33*4` | WO | `[9:0] SADCBAUD` | 波特率分频：`SARADC_CLK = Fadc_clock / [2×(SADCBAUD+1)]`（手册 §11.2 寄存器 11-4）。本工程写 `5`，6MHz/(2×6)=500kHz（test_adkey.c:69） |
| `SADCDAT0..15` | 388-403 | `SFR5_BASE + 0x20..0x2f*4` | RO | `[9:0] SADCDAT` | 10-bit 转换结果；只读低 10 位有效，高位保留为 0（手册 §11.2 寄存器 11-5） |
| `SADCDAT12` | 400 | `SFR5_BASE + 0x2c*4` | RO | 同上 | PB5/ADC12 转换结果，本工程读取寄存器（test_adkey.c:95） |

### 2.3 GPIO PORTB（手册 §3.2，sfr.h SFR6_BASE 0x10~0x1c）

PORTB 寄存器整体位于 SFR6_BASE，偏移 0x10~0x1c：

| 寄存器 | sfr.h 行号 | 偏移 | 属性 | 关键位域 | 含义 |
|:---|:---:|:---|:---:|:---|:---|
| `GPIOBDIR` | 439 | `SFR6_BASE + 0x13*4` | RW | `[5]` | `1` = 输入；本工程置 1（test_adkey.c:58） |
| `GPIOBDE` | 440 | `SFR6_BASE + 0x14*4` | RW | `[5]` | `1` = 数字输入使能（手册 §3.2；默认 0xFF，本工程置 1） |
| `GPIOBFEN` | 441 | `SFR5_BASE + 0x15*4` | RW | `[5]` | `0` = 纯 GPIO（不作功能 IO 映射）；本工程清 0（test_adkey.c:60） |
| `GPIOBPU` | 443 | `SFR6_BASE + 0x17*4` | RW | `[5]` | 10kΩ 上拉；本工程置 1（test_adkey.c:61） |
| `GPIOBPD` | 444 | `SFR6_BASE + 0x18*4` | RW | `[5]` | 10kΩ 下拉；本工程清 0（test_adkey.c:62） |
| `GPIOBPU200K` | 445 | `SFR6_BASE + 0x19*4` | RW | `[5]` | 200kΩ 上拉；本工程清 0（test_adkey.c:63） |
| `GPIOBPD200K` | 446 | `SFR6_BASE + 0x1a*4` | RW | `[5]` | 200kΩ 下拉；本工程清 0（test_adkey.c:64） |
| `GPIOBPU300` | 447 | `SFR6_BASE + 0x1b*4` | RW | `[5]` | 300Ω 上拉；本工程清 0（test_adkey.c:65） |
| `GPIOBPD300` | 448 | `SFR6_BASE + 0x1c*4` | RW | `[5]` | 300Ω 下拉；本工程清 0（test_adkey.c:66） |
| `GPIOB` | 438 | `SFR6_BASE + 0x12*4` | RW | `[5]` | 数据寄存器；本工程不读取该位 |

### 2.4 中断控制器（PIC）

| 寄存器 | sfr.h 行号 | 偏移 | 属性 | 含义（手册 §2.3） |
|:---|:---:|:---|:---:|:---|
| `PICEN` | 324 | `SFR4_BASE + 0x11*4` | RW | 中断 31~0 使能位；本工程在 TMR1 ISR 注册后置 `BIT(IRQ_TMR1_VECTOR)`（test_adkey.c:230） |
| `PICPR` | 325 | `SFR4_BASE + 0x12*4` | RW | 中断优先级选择；本工程清 `BIT(IRQ_TMR1_VECTOR)`，按 test_timer.c:113 注释即"高优先级" |
| `register_isr(vec, fn)` | （C 声明在 include.h） | — | — | 由 PICADR 跳转表把向量号绑定到 ISR；test_adkey.c:220 调用 |

`IRQ_TMR1_VECTOR = 4`（`header/int.h:7`），与手册 §2.2 表中"中断号 4 / 0x30 / Timer1 中断"一致。

---

## 3. 引脚定义与复用

### 3.1 使用的引脚

| PAD | 备注（手册 §4.2） | IO TYPE | Power | 上拉档位 | 复位后 | ADC | 与按键矩阵的连接 |
|:---|:---|:---|:---|:---|:---:|:---:|:---|
| **PB5 / WKO** | "10S Reset 主唤醒源" | TYEP1_WKO | VDDIO | 0.3K \| 10K \| 200K | hiz / INPUT | **ADC12** | PWRKEY 网络节点 |

> **重要风险提示（手册 §4.2 备注 + 附录 A.4）**：PB5 同时是 10 秒复位的主唤醒源。本测试仅将 PB5 用作 ADC 输入，不修改 `WKUPCON/WKUPEDG/WKUPCPND`、不修改 `RTCCON10/RTCCON12`（10S 复位使能寄存器）；**长按测试不要按住超过 10 秒**，否则会触发芯片复位。实测阶段五 P/P 长按 3.8 秒未复位（test_adkey.md §10.5）。

### 3.2 FUNCMCON 映射值

PB5 上电默认即 INPUT/Hiz（手册 §1 概述），**无需 FUNCMCON 任何配置即可作为 ADC12 输入**。对照 `bt892x_pinfunction.md` §3 的 FUNCMCON 映射表：

| 复用列 | 控制位域 | PB5 是否需要映射 | 原因 |
|:---|:---|:---:|:---|
| M (SD Card) | `FUNCMCON0[3:0]` | 否 | ADC 输入路径与 SD 控制器无关 |
| N (SPI0) | `FUNCMCON0[8:4]` | 否 | 同上 |
| P/Q/R (UART0/1/2) | `FUNCMCON0[11:8]` 等 | 否 | 同上 |
| S (HS UART) | `FUNCMCON0[19:16]` | 否 | 同上 |
| U/V/W (PWM-T3/4/5) | `FUNCMCON2[11:8]` 等 | 否 | 同上（虽然 PB5 也可作 PWM2-T3-G2，本测试不用） |
| AA (Timers/IR) | `IR_MAP=FUNCMCON2[23:20]` | 否 | 同上 |
| ADC | （无 FUNCMCON 域） | — | PB5 上电即被 ADC 控制器读取；只需打开 `SADCCON.ADCEN` 与对应通道使能 |

> SARADC 的"通道→引脚"映射是**硬件直连**关系（手册 §8.10 / bt892x_pinfunction.md §8.10），不走 FUNCMCON。PB5↔ADC12 是固定绑定。

### 3.3 GPIO 方向/上下拉配置（手册 §3.2）

本工程在 `test_adkey_raw_init` 中对 PB5 写出的位组合含义如下：

| 操作 | 寄存器 / 位 | 含义 |
|:---|:---|:---|
| `GPIOBDIR \|= BIT(5)` | GPIOBDIR[5] = 1 | 输入模式 |
| `GPIOBDE \|= BIT(5)` | GPIOBDE[5] = 1 | 数字 IO 使能（否则引脚保持模拟） |
| `GPIOBFEN &= ~BIT(5)` | GPIOBFEN[5] = 0 | 不映射到外设功能 IO |
| `GPIOBPU \|= BIT(5)` | GPIOBPU[5] = 1 | 10kΩ 上拉 |
| `GPIOBPD &= ~BIT(5)` | GPIOBPD[5] = 0 | 关 10kΩ 下拉 |
| `GPIOBPU200K &= ~BIT(5)` | GPIOBPU200K[5] = 0 | 关 200kΩ 上拉 |
| `GPIOBPD200K &= ~BIT(5)` | GPIOBPD200K[5] = 0 | 关 200kΩ 下拉 |
| `GPIOBPU300 &= ~BIT(5)` | GPIOBPU300[5] = 0 | 关 300Ω 上拉 |
| `GPIOBPD300 &= ~BIT(5)` | GPIOBPD300[5] = 0 | 关 300Ω 下拉 |

最终形成 **GPIO 10kΩ 上拉 + ADC12 内部 100kΩ 上拉**的双上拉结构（实测稳定；另两种单上拉组合已被实测否决，详见 test_adkey.md §7）。

---

## 4. 初始化原理

### 4.1 为什么选 x24m_clkdiv4 = 6MHz 作为 SARADC 时钟源

数据手册规定 SARADC 位时钟最大 1MHz（手册 §11.1）；用户提供的时钟树给出两个 SARADC 时钟源选项：

| `CLKCON0[28]` | 时钟源 | 选/不选理由 |
|:---:|:---|:---|
| `0` | `rc2m_clk` | 内部 2MHz RC，频率随温度/电压漂移大；2MHz / (2×(0+1)) = 1MHz 触顶，没有分频裕度 |
| `1` | `x24m_clkdiv4 = 6MHz` | 24MHz 晶振 4 分频，稳定度高；6MHz 有充分分频裕度 |

本工程置 `CLKCON0[28]=1`，再打开 `CLKGAT0[13]=1` 把 SARADC 时钟门打开（手册 §11.2）。

### 4.2 为什么 SADCBAUD = 5（500kHz）

手册 §11.2 寄存器 11-4 给出：

```
SARADC_CLK = Fadc_clock / [2 × (SADCBAUD + 1)]
```

代入 `Fadc_clock = 6MHz`：

```
500 kHz = 6 MHz / [2 × (5 + 1)]
```

500kHz 远低于手册 §11.1 的 1MHz 上限；按键采样周期 5ms（200Hz 扫描）远大于单次 SARADC 转换时间（10 bit ≈ 数十个 SARADC_CLK），所以 ADC 带宽完全不是瓶颈。该值在保证精度的同时给"未来需要提高扫描频率"留有空间。

### 4.3 为什么选通道 12（PB5）

- 板载 PWRKEY 网络接到 PB5（原理图层面）；
- 手册 §8.10 "SARADC 通道分配"明确 ADC12 ↔ PB5 绑定；
- PB5 同时可作 10S Reset 主唤醒源——所以"使用 PB5 做 ADKEY"需要得到硬件责任人确认；本测试已经在确认后使用。

### 4.4 为什么用 ADC12 内部 100kΩ 上拉 + GPIO 10kΩ 上拉"双上拉"

阶段二对上拉组合做了 3 种对照实测（test_adkey.md §7）：

| 上拉组合 | PLAY | PREV | NEXT | NONE | 结论 |
|:---|---:|---:|---:|---:|:---|
| GPIO 10k + ADC12 100k | 24 | 115 | 120 | 122/123 | 四档稳定，可映射 ✅ |
| 仅 ADC12 100k | 24 | 与 NONE 重叠 32~34 | 同左 | 33 | ❌ |
| 仅 GPIO 10k | 0 | 84 | 79~104 波动 | 92 | ❌ |

最终选择 **GPIO 10k + ADC12 100k 双上拉**，二者并联 ≈ 9.1kΩ；这样 NORM（按下 NONE 时）拉得稳、按下去时分压节点也能形成足够明显的电压梯度。`SADCCON[12] CH12PUEN` 专门控制 ADC12 这一通道的内部 100kΩ 上拉，与其他通道独立。

### 4.5 为什么 5ms 扫描周期

- TMR0 已被 main.c 占作 1ms 系统节拍；
- TMR2 已被占用为 1µs 延时基准（`tick_get()` / `tick_check_expire()`）；
- TMR1 在 `test_timer.c` 已被独立验证，本工程遵循"已验证的硬件资源优先复用"原则；
- 5ms × 200Hz 对按键抖动的覆盖已经远超人工抖动（典型 ≤10ms），但又比 1ms 扫描节省功耗与 CPU；
- 5 次连续相同值 = 25ms 消抖时间，处于"既能滤掉抖动、又不让用户感到按键延迟"的折中区间（典型 5~50ms）。

### 4.6 为什么选 700ms 长按、200ms HOLD 连发

- 700ms 长按：超过人手"短按一下"的常见上限（200~500ms），又远低于 PB5 的 10 秒复位阈值；常见消费电子约定的"长按"区间。
- 200ms HOLD：模拟人眼可感知的连续触发间隔；过短会刷屏，过长会让用户感觉失灵。HOLD 用作"音量+/音量-/快进/快退"等典型操作的连发节奏。

---

## 5. 初始化操作步骤

下面给出从上电到能稳定读 ADC12 的逐步操作清单。**严格按本顺序执行**，否则会出现以下典型问题：

| 反顺序操作 | 现象 |
|:---|:---|
| 先开通道、再开 ADCEN | `SADCCH` 写入后 ADCPND 可能不按手册行为置位，导致首轮永远超时 |
| 先读 GPIO、再开 ADCEN | 引脚浮空导致上电瞬间出现异常 raw |
| 不开 CLKGAT0 | `SARADC_CLK` 恒为 0，转换永远停在 0 |
| 不开 ADCAEN/ADCANGIO | 手册 §11.2 提示"模拟模块/IO 未自动使能"，按键模拟节点无法被内部 mux 接管 |

### 5.1 阶段一~五统一初始化（`test_adkey_raw_init`，test_adkey.c:51-79）

**Step 1 —— 选 SARADC 时钟源（手册 §11.1 + 时钟树）**

```c
CLKCON0 |= BIT(28);   // SARADC_CLK_SEL = x24m_clkdiv4 = 6MHz
```

**Step 2 —— 打开 SARADC 时钟门**

```c
CLKGAT0 |= BIT(13);   // SARADC 时钟门开启
```

**Step 3 —— PB5 配置为普通数字输入 + 10kΩ 上拉**

```c
GPIOBDIR    |=  BIT(5);   // 输入
GPIOBDE     |=  BIT(5);   // 数字输入使能
GPIOBFEN    &= ~BIT(5);   // 不映射到外设
GPIOBPU     |=  BIT(5);   // 10kΩ 上拉
GPIOBPD     &= ~BIT(5);   // 关 10kΩ 下拉
GPIOBPU200K &= ~BIT(5);   // 关 200kΩ 上拉
GPIOBPD200K &= ~BIT(5);   // 关 200kΩ 下拉
GPIOBPU300  &= ~BIT(5);   // 关 300Ω 上拉
GPIOBPD300  &= ~BIT(5);   // 关 300Ω 下拉
```

**Step 4 —— 设置 SARADC 位时钟分频**

```c
SADCBAUD = 5;   // SARADC_CLK = 6MHz / (2 × 6) = 500kHz
```

**Step 5 —— 通道建立时间（可选，本工程保持默认 0）**

```c
SADCST = 0;     // 所有通道 0 个 SARADC_CLK 建立时间
```

**Step 6 —— 使能 SARADC 模拟模块、模拟 IO、ADC 主模块、ADC12 100kΩ 上拉**

```c
SADCCON = BIT(19)    // ADCAEN
        | BIT(18)    // ADCANGIO
        | BIT(16)    // ADCEN
        | BIT(12);   // CH12PUEN
```

### 5.2 阶段三/五的 TMR1 扫描初始化（`test_adkey_timer1_init`，test_adkey.c:215-231）

**Step 7 —— 关 TMR1 中断向量与清控制寄存器**

```c
TMR1CON = 0;
PICEN  &= ~BIT(IRQ_TMR1_VECTOR);   // 暂时屏蔽 IRQ_TMR1_VECTOR(=4)
```

**Step 8 —— 注册 ISR、清 tick 计数**

```c
register_isr(IRQ_TMR1_VECTOR, test_adkey_timer1_isr);
g_adkey_scan_tick = 0;
```

**Step 9 —— 清溢出挂起并清零计数器**

```c
TMR1CPND = BIT(9);
TMR1CNT  = 0;
```

**Step 10 —— 写周期寄存器 4999（5ms）**

```c
TMR1PR = ADKEY_SCAN_PERIOD_US - 1;   // 5000 - 1 = 4999
```

**Step 11 —— 写控制寄存器：先使能 TIE（中断使能），再 OR 上时钟源与 TMREN**

```c
TMR1CON = BIT(7);               // TIE = 1
TMR1CON |= BIT(2) | BIT(0);     // INCSEL=01(计数器输入上升沿，分频至 tmr_inc=1MHz) + TMREN=1
```

> 注：`TMR1CON |= BIT(2)` 实际把 INCSEL 设为 `01`，由 `tmr_inc=1MHz` 驱动（test_timer.c 验证过的 TMR1 模式）。

**Step 12 —— 设高优先级并打开 IRQ_TMR1_VECTOR**

```c
PICPR  &= ~BIT(IRQ_TMR1_VECTOR);   // 按 test_timer.c:113 注释，"清 0 = 高优先级"
PICEN  |=  BIT(IRQ_TMR1_VECTOR);
```

### 5.3 单次采样操作（`test_adkey_raw_read`，test_adkey.c:81-97）

**Step 13 —— 写 `SADCCH = BIT(12)` 启动 ADC12 转换并清 ADCPND（手册 §11.3 步骤 4）**

```c
SADCCH = BIT(12);
start  = tick_get();   // TMR2 1µs 基准
```

**Step 14 —— 轮询 ADCPND（手册 §11.3 步骤 5），最长 5ms 超时保护**

```c
while (!(SADCCH & BIT(16))) {
    if (tick_check_expire(start, 5000)) return false;
}
```

**Step 15 —— 读 SADCDAT12[9:0]（手册 §11.3 步骤 6）**

```c
*raw = SADCDAT12 & 0x03FF;
return true;
```

---

## 6. 代码详解

### 6.1 宏与全局变量（test_adkey.c:19-49）

```c
#define ADKEY_PIN_MASK              BIT(5)         // PB5

#define SARADC_CLK_SEL_X24M_DIV4    BIT(28)        // CLKCON0[28]
#define SARADC_CLK_GATE             BIT(13)        // CLKGAT0[13]
#define SARADC_AUTO_ANALOG_EN       BIT(19)        // SADCCON[19] ADCAEN
#define SARADC_AUTO_ANALOG_IO_EN    BIT(18)        // SADCCON[18] ADCANGIO
#define SARADC_ADC_EN               BIT(16)        // SADCCON[16] ADCEN
#define SARADC_CH12_PULLUP_EN       BIT(12)        // SADCCON[12] CH12PUEN
#define SARADC_CH12_EN              BIT(12)        // SADCCH[12] CH12EN
#define SARADC_ADCPND               BIT(16)        // SADCCH[16] ADCPND

#define SARADC_BAUD_500KHZ          5u
#define SARADC_DATA_MASK            0x03ffu        // SADCDAT12[9:0]
#define SARADC_TIMEOUT_US           5000u
#define ADKEY_PRINT_PERIOD_MS       100u
#define ADKEY_WINDOW_SAMPLES        10u

// 实测阈值（test_adkey.md §8.2）
#define ADKEY_PLAY_MAX              69u
#define ADKEY_PREV_MAX              117u
#define ADKEY_NEXT_MAX              120u
#define ADKEY_NONE_MIN              122u

// TMR1 时基与状态机常量
#define ADKEY_SCAN_PERIOD_US        5000u          // 5ms
#define ADKEY_DEBOUNCE_SAMPLES      5u             // 25ms 消抖
#define ADKEY_LONG_MS               700u
#define ADKEY_LONG_TICKS            ((ADKEY_LONG_MS * 1000u) / ADKEY_SCAN_PERIOD_US)   // 140
#define ADKEY_HOLD_MS               200u
#define ADKEY_HOLD_TICKS            ((ADKEY_HOLD_MS * 1000u) / ADKEY_SCAN_PERIOD_US)   // 40

static volatile u32 g_adkey_scan_tick = 0;
```

**解释**：

- `ADKEY_PIN_MASK` 统一表示"操作 PB5 时要写 1 的那一位"，下文所有 `|=` / `&=` 操作都靠它定位位 5。
- 8 个 SARADC 宏对应手册 §11.2 寄存器 11-1、11-2 的关键位，避免源码里出现裸 `BIT(19)` 之类的"魔法数"。
- `SARADC_DATA_MASK = 0x3FF`：手册 §11.2 寄存器 11-5 规定 `SADCDAT12` 只有低 10 位有效，必须 mask 掉高 22 位噪声（虽然理论上硬件不会写脏位）。
- `ADKEY_PLAY_MAX / PREV_MAX / NEXT_MAX / NONE_MIN` 四个阈值取自 test_adkey.md §8.2 表"相邻稳定值中点"原则；121 保留为 NEXT/NONE 死区，输出 `KEY_UNKNOWN`。
- `LONG_TICKS = 140`、`HOLD_TICKS = 40` 是把毫秒转换为 TMR1 tick（每 tick 5ms）的纯编译期算式，避免运行时除法。
- `g_adkey_scan_tick` 是 `volatile u32`：TMR1 ISR 写、main loop 读，必须 `volatile` 防止编译器优化掉"重复读同一变量"。

### 6.2 初始化函数 `test_adkey_raw_init`（test_adkey.c:51-79）

```c
51  static void test_adkey_raw_init(void)
52  {
53      // SARADC 时钟：x24m_clkdiv4 = 6MHz，并打开 SARADC 时钟门。
54      CLKCON0 |= SARADC_CLK_SEL_X24M_DIV4;
55      CLKGAT0 |= SARADC_CLK_GATE;
56
57      // PB5：普通数字输入；恢复阶段一已验证的 GPIO 10K + ADC12 100K 双上拉。
58      GPIOBDIR    |=  ADKEY_PIN_MASK;
59      GPIOBDE     |=  ADKEY_PIN_MASK;
60      GPIOBFEN    &= ~ADKEY_PIN_MASK;
61      GPIOBPU     |=  ADKEY_PIN_MASK;
62      GPIOBPD     &= ~ADKEY_PIN_MASK;
63      GPIOBPU200K &= ~ADKEY_PIN_MASK;
64      GPIOBPD200K &= ~ADKEY_PIN_MASK;
65      GPIOBPU300  &= ~ADKEY_PIN_MASK;
66      GPIOBPD300  &= ~ADKEY_PIN_MASK;
67
68      // SARADC_CLK = 6MHz / (2 * (5 + 1)) = 500kHz。
69      SADCBAUD = SARADC_BAUD_500KHZ;
70
71      // 数据手册将通道建立时间标为可选，阶段一保持默认 0 SARADC_CLK。
72      SADCST = 0;
73
74      // 自动使能模拟模块和模拟 IO，同时使能 ADC 及 ADC12 100K 上拉。
75      SADCCON = SARADC_AUTO_ANALOG_EN
76              | SARADC_AUTO_ANALOG_IO_EN
77              | SARADC_ADC_EN
78              | SARADC_CH12_PULLUP_EN;
79  }
```

**逐段解释**：

- **行 54-55**：选 6MHz 稳定时钟源 + 开时钟门。**先开时钟、后写寄存器**，否则第一次写 `SADCBAUD/SADCCON` 可能被门关掉而丢失。
- **行 58-66**：把 PB5 配置成"输入 + 数字 IO + 10k 上拉、其余 5 档上下拉全关"。这是阶段二 §7 实测后唯一能给出四档稳定值的组合。**注意不写 `GPIOBDRV`**——输入模式下输出驱动档位无意义，留默认即可。
- **行 69**：写 `SADCBAUD=5` 计算得到 500kHz ADC 位时钟。手册 §11.2 寄存器 11-4 标注该字段为 WO（只写语义），但工具链里仍以 RW 宏定义，读它没有副作用、但写完不需要回读。
- **行 72**：写 `SADCST=0`，所有通道建立时间为 0 个 SARADC_CLK。手册 §11.2 寄存器 11-3 说"可选"，本工程按键分压网络的 RC 时间常数远小于 1 个 500kHz 时钟周期（2µs），保持 0 不会丢失精度。
- **行 75-78**：一次性把 4 个位置 1。其中：
  - `ADCAEN(19)=1`：模拟模块自动使能——手册明确要求，否则按键模拟节点无法被内部 mux 接管。
  - `ADCANGIO(18)=1`：模拟 IO 自动使能——同上。
  - `ADCEN(16)=1`：SARADC 主使能。
  - `CH12PUEN(12)=1`：ADC12 内部 100kΩ 上拉。
  - **没有置 `ADCIE(17)`**：阶段一~三保持轮询，不进 SARADC 中断。
  - **没有置其他 `CHnPUEN`**：只开通道 12，其他通道保持默认 0 上拉。

### 6.3 单次采样函数 `test_adkey_raw_read`（test_adkey.c:81-97）

```c
81  static bool test_adkey_raw_read(u32 *raw)
82  {
83      u32 start;
84
85      // 每次写 CH12EN 都会清 ADCPND，并启动一次新的 ADC12 转换。
86      SADCCH = SARADC_CH12_EN;
87      start = tick_get();
88
89      while (!(SADCCH & SARADC_ADCPND)) {
90          if (tick_check_expire(start, SARADC_TIMEOUT_US)) {
91              return false;
92          }
93      }
94
95      *raw = SADCDAT12 & SARADC_DATA_MASK;
96      return true;
97  }
```

**逐段解释**：

- **行 86**：手册 §11.3 步骤 4——写 `SADCCH=BIT(12)` 同时做三件事：①使能通道 12；②清 ADCPND；③启动硬件转换。下一行 `tick_get()` 立刻读取 µs 基准，避免把"写 SADCCH 到 tick_get"的几十个时钟周期算进超时。
- **行 89-93**：手册 §11.3 步骤 5——轮询 `ADCPND`。理论单次转换 ≈ 数十个 SARADC_CLK（500kHz 下百 µs 级），留 5000µs = 5ms 超时足矣；若芯片异常（例如 ADCEN 没开），5ms 后报 `[TIMEOUT]`，避免测试静默卡死。
- **行 95**：手册 §11.3 步骤 6——读 `SADCDAT12` 并 mask 掉高 22 位。

### 6.4 TMR1 ISR（test_adkey.c:208-213）

```c
208  AT(.com_text.isr)
209  static void test_adkey_timer1_isr(void)
210  {
211      TMR1CPND = BIT(9);
212      g_adkey_scan_tick++;
213  }
```

**逐段解释**：

- **行 208**：`AT(.com_text.isr)` 是本工程链接段属性宏，要求把 ISR 放到 `.com_text.isr` 段，与 RAM 拷贝 / 链接布局约束保持一致。
- **行 211**：手册 §4.2 TMR0/1/2 寄存器——写 `TMR1CPND[9]=1` 清溢出挂起，是中断返回的必要条件，否则会反复进同一 ISR。
- **行 212**：递增全局 tick。**ISR 中不调用 `printf`**，避免长串口阻塞打断 5ms 节拍。

### 6.5 TMR1 初始化 `test_adkey_timer1_init`（test_adkey.c:215-231）

```c
215  static void test_adkey_timer1_init(void)
216  {
217      TMR1CON = 0;
218      PICEN &= ~BIT(IRQ_TMR1_VECTOR);
219
220      register_isr(IRQ_TMR1_VECTOR, test_adkey_timer1_isr);
221      g_adkey_scan_tick = 0;
222
223      TMR1CPND = BIT(9);
224      TMR1CNT = 0;
225      TMR1PR = ADKEY_SCAN_PERIOD_US - 1;
226      TMR1CON = BIT(7);
227      TMR1CON |= BIT(2) | BIT(0);
228
229      PICPR &= ~BIT(IRQ_TMR1_VECTOR);
230      PICEN |= BIT(IRQ_TMR1_VECTOR);
231  }
```

**逐段解释**：

- **行 217-218**：先把 `TMR1CON` 清零，再屏蔽 PIC 上的 IRQ_TMR1_VECTOR。**避免初始化过程中意外触发 ISR**（PR 与 CNT 配合不当时可能瞬间产生挂起）。
- **行 220**：调用 include 提供的 `register_isr(vec, fn)` 写 PICADR 跳转表。
- **行 223-225**：清挂起 → 清计数器 → 设周期 4999。
- **行 226**：先写 `TMR1CON = BIT(7)` ——只开 `TIE`（中断使能），但 `TMREN=0` 此时还在停机状态。
- **行 227**：`|= BIT(2) | BIT(0)`——把 `INCSEL` 设为 `01`（计数器输入上升沿，对应 `tmr_inc=1MHz`，见 test_timer.c 的验证）并 `TMREN=1` 启动。
- **行 229-230**：清 PICPR[4]（按 test_timer.c:113 注释"清 0 = 高优先级"），再开 IRQ_TMR1_VECTOR。

### 6.6 阶段三/五的主循环：扫描 + 消抖 + 边沿消息（test_adkey.c:271-308、343-434）

阶段三与阶段五的主循环共用同一段状态机骨架，差异仅在于 LONG/HOLD 的附加分支。这里把阶段五完整列出并分块解读：

```c
343      while (1) {
344          current_tick = g_adkey_scan_tick;
345          if (current_tick == handled_tick) {
346              continue;
347          }
348          handled_tick = current_tick;
349
350          if (!test_adkey_raw_read(&raw)) { ... }
351
356          sampled_key = test_adkey_map_raw(raw);
357          if (sampled_key == KEY_UNKNOWN) {
358              candidate_key = KEY_UNKNOWN;
359              same_count = 0;
360              continue;
361          }
362
363          if (sampled_key != candidate_key) {
364              candidate_key = sampled_key;
365              same_count = 1;
366              continue;
367          }
368
369          if (same_count < ADKEY_DEBOUNCE_SAMPLES) {
370              same_count++;
371          }
372
373          if ((same_count >= ADKEY_DEBOUNCE_SAMPLES) &&
374              (candidate_key != stable_key)) {
375              u8 old_key = stable_key;
376              held_ms = (current_tick - press_tick) *
377                        (ADKEY_SCAN_PERIOD_US / 1000u);
378
379              if (old_key != KEY_NONE) {
380                  u16 release_message;
381                  if (long_sent) {
382                      release_message = (u16)(KEY_LONG_UP | old_key);
383                      TEST_LOG("msg=0x%04x KEY_LONG_UP %s held=%u ms raw=%u",
384                               (u32)release_message,
385                               test_adkey_key_name(old_key), held_ms, raw);
386                  } else {
387                      release_message = (u16)(KEY_SHORT_UP | old_key);
388                      TEST_LOG("msg=0x%04x KEY_SHORT_UP %s held=%u ms raw=%u",
389                               (u32)release_message,
390                               test_adkey_key_name(old_key), held_ms, raw);
391                  }
392              }
393
394              stable_key = candidate_key;
395              long_sent = false;
396              long_tick = 0;
397
398              if (stable_key != KEY_NONE) {
399                  u16 press_message = (u16)(KEY_SHORT | stable_key);
400                  press_tick = current_tick;
401                  TEST_LOG("msg=0x%04x KEY_SHORT %s raw=%u",
402                           (u32)press_message,
403                           test_adkey_key_name(stable_key), raw);
404              }
405          }
406
407          if ((stable_key != KEY_NONE) &&
408              (sampled_key == stable_key) &&
409              !long_sent &&
410              ((u32)(current_tick - press_tick) >= ADKEY_LONG_TICKS)) {
411              u16 long_message = (u16)(KEY_LONG | stable_key);
412              held_ms = (current_tick - press_tick) *
413                        (ADKEY_SCAN_PERIOD_US / 1000u);
414              long_sent = true;
415              long_tick = current_tick;
416              TEST_LOG("msg=0x%04x KEY_LONG %s held=%u ms raw=%u",
417                       (u32)long_message,
418                       test_adkey_key_name(stable_key), held_ms, raw);
419          }
420
421          if ((stable_key != KEY_NONE) &&
422              (sampled_key == stable_key) &&
423              long_sent &&
424              ((u32)(current_tick - long_tick) >= ADKEY_HOLD_TICKS)) {
425              u16 hold_message = (u16)(KEY_HOLD | stable_key);
426              held_ms = (current_tick - press_tick) *
427                        (ADKEY_SCAN_PERIOD_US / 1000u);
428              long_tick = current_tick;
429              TEST_LOG("msg=0x%04x KEY_HOLD %s held=%u ms raw=%u",
430                       (u32)hold_message,
431                       test_adkey_key_name(stable_key), held_ms, raw);
432          }
433      }
```

**逐段解释**：

- **行 344-348**：经典的"消费 tick"模式。每 5ms ISR 增 1；main loop 看到 tick 增长才进入状态机；打印延迟导致的 tick 跳过会延长实际消抖时间（test_adkey.md §9.2 末段"主循环若因打印错过一个 TMR1 tick，只会延长实际消抖时间"）。
- **行 350**：调 `test_adkey_raw_read` 拿到最新 raw；超时则 `continue` 不进入状态机。
- **行 356**：用阶段二测得的阈值把 raw 折成 `sampled_key` ∈ {NONE/PLAY/PREV/NEXT/UNKNOWN}。
- **行 357-361**：raw=121（NEXT/NONE 死区）映射成 UNKNOWN，状态机立即丢弃计数，避免把噪声记成"5 次相同"。
- **行 363-367**：新候选键与上一候选不同——重置候选并把 same_count 重置为 1（**注意重置为 1 而非 0**，因为这次采样本身算 1 次有效样本，下一轮再加 1 才能达到 5）。
- **行 369-371**：相同键，same_count 累加，封顶 5（`if (same_count < 5) same_count++;`）。
- **行 373-405**：当 same_count ≥ 5 且候选键与稳定键不同——发出"释放旧键 + 按下新键"两条消息。注意：
  - **释放消息的类型由 long_sent 决定**——若之前已经发出 LONG（说明本次按下达到 700ms），则本次松开必须发 `KEY_LONG_UP` 而不是 `KEY_SHORT_UP`，符合 test_adkey.md §10.1 表。
  - **行 394-396**：更新 stable_key、重置 long_sent 与 long_tick；这是"上一个稳定键已经结束"的状态清理。
  - **行 398-404**：新键非 NONE 时，**记 press_tick 并发 KEY_SHORT**。
- **行 407-419**：LONG 触发——稳定键非 NONE、未发过 LONG、按压时长 ≥ 140 ticks（700ms）。发 `KEY_LONG`，**记 long_sent=true、long_tick=current_tick**。
- **行 421-432**：HOLD 触发——稳定键非 NONE、已经发过 LONG、从 long_tick 起 ≥ 40 ticks（200ms）。发 `KEY_HOLD`，**更新 long_tick**，保持"每 200ms 一次"的稳定节拍。
- **无符号减法回绕安全**：`(u32)(current_tick - press_tick)` 用 u32 减法，2^32 × 5ms ≈ 68 年才回绕，长期运行安全。

### 6.7 键值映射 `test_adkey_map_raw`（test_adkey.c:147-162）

```c
147  static u8 test_adkey_map_raw(u32 raw)
148  {
149      if (raw <= ADKEY_PLAY_MAX)   return KEY_PLAY;   // <=69
150      if (raw <= ADKEY_PREV_MAX)   return KEY_PREV;   // <=117
151      if (raw <= ADKEY_NEXT_MAX)   return KEY_NEXT;   // <=120
152      if (raw >= ADKEY_NONE_MIN)   return KEY_NONE;   // >=122
153      return KEY_UNKNOWN;                              // =121
154  }
```

按 test_adkey.md §8.2 表："PLAY/PREV 和 PREV/NEXT 使用相邻稳定值中点；NEXT/NONE 之间仅有 ADC=121，因此将 121 保留为死区"。这个映射**只是单次 ADC 值分类**，没有消抖也没有时间窗——按 test_adkey.md §8.2 末段"按住时每 100ms 重复打印、按下或抬起瞬间出现中间状态，均属于本阶段预期行为"。

---

## 7. 测试步骤与预期现象

> 本节实测数据全部引用自 `docs/test_adkey.md`，并标注章节号。开发板上电后默认打印 `Hello SMART Flash MiniProj` 之后由 main.c 选择进入的测试入口。

### 7.1 测试接线

PWRKEY 网络已经在板载布线中完成 PB5/ADC12 的连接，**测试无需额外接线**：

| 按键 | 键名 | PWRKEY 到 GND 的电阻 |
|:---|:---:|---:|
| S5 P/P | PLAY | 0Ω（直连 GND） |
| S6 PREV | PREV | 12kΩ |
| S7 NEXT | NEXT | 47kΩ |

注意：板载 3 键中 S5 P/P 是板上的 PLAY/PAUSE 单键，本测试中对应 `KEY_PLAY`。

### 7.2 入口宏开启位置（main.c:73-77）

```c
// #define TEST_ADKEY_RAW_EN      1
// #define TEST_ADKEY_MAP_EN      1
// #define TEST_ADKEY_DEBOUNCE_EN 1
// #define TEST_ADKEY_LONG_EN     1
// #define TEST_ADKEY_HOLD_EN     1
```

取消对应行注释、确保**同一时间只有一个宏为 1**（main.c:327-341 用 `#ifdef` 分发，后定义会覆盖前定义，串口只看到最后一次入口的输出）。然后 Build → Downloader 烧录新固件。

### 7.3 阶段一：原始 ADC 采样（`TEST_ADKEY_RAW_EN`）

**接线**：不需要额外接线，使用板载 3 键。

**按键动作顺序**：NONE → PLAY → PREV → NEXT → NONE（每个状态持续按住 ≥1 秒）。

**串口预期启动信息**（test_adkey.c:107-113）：

```text
[TEST] ========================================
[TEST] ADKEY stage 1: PB5 / ADC12 raw sampling
[TEST] SARADC clock: x24m/4=6MHz, baud=5, ADC clock=500kHz
[TEST] PB5 pull-up: GPIO 10K + ADC12 100K
[TEST] SARADC analog auto-enable: ADCAEN=1, ADCANGIO=1
[TEST] Press in order: NONE -> PLAY -> PREV -> NEXT
[TEST] ========================================
```

**每个 100ms 打印一行 raw**：

```text
[TEST] ADC12 raw=122 sample=10
```

**每 10 个样本（1 秒窗口）汇总一次**：

```text
[TEST] 1s window: min=122 max=122 span=0
```

**预期实测窗口**（test_adkey.md §4.3 表）：

| 状态 | 稳定窗口 | 稳定中心值 | 与上一档间隔 |
|:---|:---|---:|---:|
| PLAY（P/P） | `min=24 max=24 span=0` | 24 | — |
| PREV | `min=115 max=115 span=0` | 115 | 91 LSB |
| NEXT | `min=120 max=120 span=0` | 120 | 5 LSB |
| NONE | `min=122 max=122 span=0` | 122 | 2 LSB |

**判定**：每档稳定时 `span=0`、互不相同、全程不出现 `[TIMEOUT]` 即可视为阶段一通过。

### 7.4 阶段二：原始值→键值映射（`TEST_ADKEY_MAP_EN`）

**接线**：同阶段一。

**按键动作**：分别按 4 个状态各 ≥1 秒，再做几次"按下→松开"快速切换。

**串口预期启动信息**（test_adkey.c:185-190）：

```text
[TEST] ========================================
[TEST] ADKEY stage 2: PB5 / ADC12 key mapping
[TEST] PB5 pull-up: GPIO 10K + ADC12 100K
[TEST] Map: <=69 PLAY, <=117 PREV, <=120 NEXT, 121 UNKNOWN, >=122 NONE
[TEST] No debounce yet: repeated lines and transition UNKNOWN are expected
[TEST] ========================================
```

**四种稳定状态串口输出**（test_adkey.md §8.3）：

```text
[TEST] ADC12 raw=123 -> key=NONE code=0x00
[TEST] ADC12 raw=24 -> key=PLAY code=0x01
[TEST] ADC12 raw=115 -> key=PREV code=0x02
[TEST] ADC12 raw=120 -> key=NEXT code=0x03
```

**判定**：

- 空闲期间连续 NONE，无误报 NEXT；
- PLAY/PREV/NEXT 持续按住时连续对应按键；
- 快速按下/松开时出现 KEY_UNKNOWN 与 KEY_NONE 交替——这是预期的；
- 全程不出现 ADC 超时。

### 7.5 阶段三：5ms×5 消抖（`TEST_ADKEY_DEBOUNCE_EN`）

**接线**：同阶段一。

**按键动作**：

| 子测试 | 操作 | 期望 |
|:---|:---|:---|
| Test 1 | PLAY/PREV/NEXT 各按一次（短按） | 每键一对 SHORT + SHORT_UP，键值正确 |
| Test 2 | 每键连续短按 10 次 | 每键正好 10 对消息，不多报、不漏报、不串键 |
| Test 3 | 每键持续按住约 2 秒 | 按下只报一次，保持期间无重复，松开只报一次 |

**串口预期启动信息**（test_adkey.c:260-265）：

```text
[TEST] ========================================
[TEST] ADKEY stage 3: 5ms x 5 debounce
[TEST] TMR1 scan period: 5ms; stable count: 5; debounce: 25ms
[TEST] Expected events: KEY_SHORT on stable press, KEY_SHORT_UP on stable release
[TEST] Long press and repeat are NOT implemented in this stage
[TEST] ========================================
```

**每键短按一次的预期**（test_adkey.md §9.5）：

```text
[TEST] msg=0x0001 KEY_SHORT PLAY raw=24
[TEST] msg=0x0801 KEY_SHORT_UP PLAY raw=123
[TEST] msg=0x0002 KEY_SHORT PREV raw=115
[TEST] msg=0x0802 KEY_SHORT_UP PREV raw=123
[TEST] msg=0x0003 KEY_SHORT NEXT raw=120
[TEST] msg=0x0803 KEY_SHORT_UP NEXT raw=124
```

**每键连按 10 次的统计**（test_adkey.md §9.5 表）：

| 按键 | SHORT 数量 | SHORT_UP 数量 | 错键/额外消息 |
|:---|---:|---:|---:|
| PLAY | 10 | 10 | 0 |
| PREV | 10 | 10 | 0 |
| NEXT | 10 | 10 | 0 |

**判定**：

- 每对 SHORT/SHORT_UP 配对正确；
- 持续按住约 2 秒不重复；
- NEXT 连按 10 次全部得到 `0x0003/0x0803`，证明 120 vs 123 的双上拉组合在 5 次连续采样规则下可稳定区分；
- 不出现 ADC 超时、KEY_UNKNOWN、漏报或串键。

### 7.6 阶段四：700ms 长按（`TEST_ADKEY_LONG_EN`）

**当前状态**：**已并入阶段五**，函数体被替换为引导日志（test_adkey.c:310-317）：

```c
void test_adkey_long_run(void)
{
    TEST_LOG("ADKEY stage 4 has been merged into stage 5 (HOLD)");
    TEST_LOG("Please enable TEST_ADKEY_HOLD_EN instead");
    while (1) { delay_ms(1000); }
}
```

这是为了释放 `.comm` 段约 110 字节空间，避免阶段四+五同时链接造成 20 字节溢出（test_adkey.md §12）。阶段四的 SHORT/LONG/LONG_UP 行为由 `test_adkey_hold_run` 完整复现。

### 7.7 阶段五：700ms 长按 + 200ms HOLD 连发（`TEST_ADKEY_HOLD_EN`）

**接线**：同阶段一。

**按键动作**：见 test_adkey.md §11.4 七组子测试。

**串口预期启动信息**（test_adkey.c:333-337）：

```text
[TEST] ========================================
[TEST] ADKEY stage 5: 200ms HOLD repeat
[TEST] Scan: TMR1 5ms; debounce: 5 samples (25ms)
[TEST] Events: SHORT -> LONG at 700ms -> HOLD every 200ms -> LONG_UP on release
[TEST] ========================================
```

**短按预期**（test_adkey.md §11.4 Test 1）：

```text
PLAY: 0x0001 -> 0x0801 (held=75 ms)
PREV: 0x0002 -> 0x0802 (held=95 ms)
NEXT: 0x0003 -> 0x0803 (held=75 ms)
```

未出现 LONG/HOLD/LONG_UP。

**长按约 1.5~1.9 秒典型序列**（test_adkey.md §11.4 Test 3）：

```text
PLAY 1605 ms: 0x0001 -> 0x0a01 (held=700 ms)
                0x0e01 (held=900 ms)   0x0e01 (held=1100 ms)
                0x0e01 (held=1300 ms)  0x0e01 (held=1500 ms)
                0x0c01 (held=1605 ms)
```

**长按 2.0~2.3 秒典型序列**（test_adkey.md §11.4 Test 5）：

```text
PLAY 1960 ms: 0x0001 -> 0x0a01 -> 0x0e01 x 6 -> 0x0c01
PREV 2320 ms: 0x0002 -> 0x0a02 -> 0x0e02 x 8 -> 0x0c02
NEXT 1985 ms: 0x0003 -> 0x0a03 -> 0x0e03 x 6 -> 0x0c03
```

**判定**：

- SHORT 在 25ms 消抖完成时发送；
- LONG 在 SHORT 之后 700ms（实际 ≈725ms，含 25ms 消抖）发送，且只发一次；
- HOLD 在 LONG 之后每 200ms 重复；
- 700ms 前松开发 SHORT_UP；700ms 后松开发 LONG_UP；
- 长按期间没有重复消息；
- NEXT 同样能正确识别 LONG/HOLD/LONG_UP，未受阈值死区影响；
- 没有 ADC 转换超时、串键或事件重复；
- P/P 长按测试中按住 3.8 秒未触发 PB5 的 10S Reset（符合手册 §4.2 + 附录 A.4 提示的 10 秒阈值）。

**消息值总表**（test_adkey.md §11.1）：

| 按键 | SHORT | SHORT_UP | LONG | LONG_UP | HOLD |
|:---|:---:|:---:|:---:|:---:|:---:|
| PLAY | `0x0001` | `0x0801` | `0x0a01` | `0x0c01` | `0x0e01` |
| PREV | `0x0002` | `0x0802` | `0x0a02` | `0x0c02` | `0x0e02` |
| NEXT | `0x0003` | `0x0803` | `0x0a03` | `0x0c03` | `0x0e03` |

---

## 8. 审查小节（对照手册与引脚定义）

### 8.1 一致性核对结论

| 核对项 | 工程做法 | 手册/引脚表依据 | 一致性结论 |
|:---|:---|:---|:---:|
| PB5 ↔ ADC12 通道绑定 | `SADCCH=BIT(12)` 启动 ADC12 | bt892x_pinfunction.md §8.10 "ADC12 → PB5" | ✅ |
| PB5 上电默认 INPUT/Hiz | 未写 GPIOBDIR 时默认输入；FUNCMCON 无需配置 | bt892x_pinfunction.md §1 + §4.2；手册 §3.2 `GPIOxDIR` 默认 0xFF | ✅ |
| SARADC 时钟源选 6MHz | `CLKCON0[28]=1` | 数据手册 §11.1；用户提供的时钟树 | ✅ |
| SARADC 时钟门打开 | `CLKGAT0[13]=1` | 时钟树 | ✅ |
| 位时钟 500kHz | `SADCBAUD=5` | 手册 §11.2 寄存器 11-4 公式 | ✅ |
| 4 个 SADCCON 位同时置 1 | `ADCAEN\|ADCANGIO\|ADCEN\|CH12PUEN` | 手册 §11.2 寄存器 11-1 | ✅ |
| 写 SADCCH 清 ADCPND 并启动 | `SADCCH=BIT(12)` | 手册 §11.2 寄存器 11-2 + §11.3 步骤 4 | ✅ |
| 读 SADCDAT12 mask 低 10 位 | `& 0x3FF` | 手册 §11.2 寄存器 11-5 | ✅ |
| 不修改 WKUPCON/WKUPEDG/WKUPCPND | 代码无相关写入 | bt892x_pinfunction.md 附录 A.4 唤醒源 | ✅ |
| TMR1 周期 4999 | `TMR1PR = ADKEY_SCAN_PERIOD_US - 1` | 手册 §4.2 TMR0/1/2 "周期 = TMRPR + 1" | ✅ |
| TMR1 ISR 清挂起 | `TMR1CPND = BIT(9)` | 手册 §4.2 TMRxCPND[9] TPCLR | ✅ |
| TMR1 优先级"清 0 = 高优先级" | `PICPR &= ~BIT(IRQ_TMR1_VECTOR)` | 沿用 test_timer.c:113 的成熟做法 | ✅ |
| `IRQ_TMR1_VECTOR = 4` | `header/int.h:7` | 手册 §2.2 "中断号 4 / 0x30 / Timer1 中断" | ✅ |
| 阈值 121 保留为 KEY_UNKNOWN 死区 | test_adkey.c:147-162 | test_adkey.md §8.2 实测 | ✅ |
| 不修改 RTCCON10/RTCCON12 | 代码无相关写入 | 避免误用 PB5 的 10S 复位 | ✅ |

### 8.2 记录在案的疑点（只记录，不要求改代码）

> 这些疑点均来源于手册说明或工程现状描述，不构成"必须修复"的清单。仅作未来维护与代码审查的备忘。

1. **手册与文档之间存在表述差异**：docs/bt892x_pinfunction.md §4.2 中 PB5 类型写作 `TYEP1_WKO`（疑为 `TYPE1_WKO` 的笔误）；sfr.h 表头注释为 `PORTB 引脚 (PB0–PB5)`。该拼写不影响功能，但维护文档时建议一并修正。

2. **PB5 同时是 10S Reset 主唤醒源**：手册 §4.2 备注 + bt892x_pinfunction.md 附录 A.4 双重强调。本测试已经得到用户"允许用作 ADKEY 但仅作输入"的确认。**未来若要把 PB5 释放作其他用途（如通用 GPIO 输出、UART RX），必须先与硬件确认 10S Reset 是否被禁用**。

3. **手册缺失**：BT892X_UserManual_Driver.md §10 自身承认"引脚定义图（Pinout）、时序图、应用电路图、存储器映射、时钟树"在原 PDF 中未提供；本工程 SARADC 时钟树的"x24m_clkdiv4 = 6MHz"描述来自"用户提供的时钟树"。如果后续芯片升级或时钟树修订，`CLKCON0[28]` 的语义需要重新核对。

4. **NEXT/NONE 阈值间隔仅 2~3 LSB**：test_adkey.md §4.4 末段"⚠️ 后续风险"提醒——温度、电源、按键重复操作下 NEXT=120 与 NONE=122/123 仍可能漂移重叠。当前阶段三的 5 次连续采样规则提供了统计层面的保护，但产品化时建议增加温度/电压补偿或者改用不同的串联电阻比例以拉开裕度。

5. **阶段四函数被刻意最小化**：test_adkey.md §12 详述——为规避 `.comm` 段 32kB 上限，`test_adkey_long_run` 当前仅作引导日志；阶段四行为完全由 `test_adkey_hold_run` 实现。如果未来需要把 LONG 与 HOLD 拆成独立入口（比如 HOLD 不再需要），要重新评估 `.comm` 段容量，必要时回退阶段四实现或精简其他测试入口。

6. **测试精度风险**：test_adkey.md §7.1 末段"长按 P/P 后芯片复位"提示——虽然 3.8 秒内未触发 10S Reset，但任何超过 10 秒的连续按住都会复位芯片。教学/演示时务必提示用户"按一下、看一眼串口、再按一次"。

7. **寄存器属性细节**：sfr.h:407 把 `SADCST` 标注为 `SFR_WO`（只写），但手册 §11.2 寄存器 11-3 也标注 `WO`。然而 `SADCCON` 在 sfr.h:405 是 `SFR_RW`，手册 §11.2 寄存器 11-1 把绝大多数位标注为 `WR`（部分位 `R`）。这意味着读 `SADCCON` 的某些位可能读不到真实硬件值（手册 §11.2 寄存器 11-1 表格中 `[31:20]` 是 `R`，其余是 `WR`）。本工程全部用 `SADCCON =` 直接覆盖式赋值，不存在回读差异。

8. **未启用 SARADC 中断**：阶段一~五全程采用 `SADCCH[16] ADCPND` 轮询，没有置 `SADCCON[17] ADCIE=1`。手册 §11.2 寄存器 11-1 把 `ADCIE` 标为 `WR`（可读写），未来若要进 SARADC 中断以节省 CPU，需要查清"PIC 上对应的中断向量号"——本工程 SARADC 章节未在 PIC 速查中列出，需要补充。

9. **`ADKEY_WINDOW_SAMPLES = 10`**（test_adkey.c:34）只在阶段一用，配合 `ADKEY_PRINT_PERIOD_MS = 100` 形成 1 秒窗口。阶段二起该宏不再被引用，保留仅为兼容历史。如果未来精简代码，可以删除该宏。

10. **AT(.com_text.isr) 段属性**：test_adkey.c:208 的 `AT(.com_text.isr)` 把 ISR 放到特定段，需 `ram.ld` 链接脚本支持。test_adkey.md §12.2 引用了 `ram.ld` 的 `.comm` 段 32kB 上限，链接脚本本身未在本节展示，但若链接脚本发生调整，ISR 段位置需同步检查。

---

> **文档版本**：基于 test/test_adkey.c v=阶段五实现 + test_adkey.md 验收报告（2026-07-20）。
> **后续维护提示**：当阶段六及以后（例如"ADC 同时采集电池电压 VBATDIV2"、"省电模式下的 ADC 唤醒"）加入时，应复用本节"5.1 阶段一~五统一初始化"作为最小骨架，并在本文档追加对应阶段小节。

---

以上内容已逐项基于 `sfr.h` 的真实 SFR 定义与五份参考文档核对，可直接落盘为 `docs/periph_adkey.md`。