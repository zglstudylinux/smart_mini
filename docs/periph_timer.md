# BT892X TIM 定时器外设文档（TMR0/1/2 定时中断 + TMR3 三路 PWM）

> **编写日期**：2026-07-21  
> **适用芯片**：中科蓝讯 BT892X（32-bit RISC-V SoC）  
> **工程**：smart_mini（CodeBlocks 工程 `smart_mini/smart_mini/smart_mini/app.cbp`）  
> **手册依据**：`docs/BT892X_UserManual_Driver.md` §4（定时器）/ §3.3（FUNCMCON2）  
> **引脚依据**：`docs/bt892x_pinfunction.md` §4.2（PORTB）/ §5.5（PWM）  
> **测试报告**：`docs/test_timer.md`  
> **寄存器定义**：`smart_mini/smart_mini/smart_mini/header/sfr.h`  
> **代码风格**：寄存器裸操作，无独立 driver 层；每个外设由 `test/test_<name>.c/.h` 配套。

---

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 外设概述

BT892X 集成 **6 路 32 位定时器**，分为两组（手册 §4.1）：

| 组 | 通道 | 能力 | 中断号 |
|---|---|---|---|
| 基础定时器 | **TMR0** / **TMR1** / **TMR2** | 仅 32 位定时 | IRQ_TMR0_VECTOR(3) / IRQ_TMR1_VECTOR(4) / IRQ_TMR2_VECTOR(5) |
| 多功能定时器 | **TMR3** / **TMR4** / **TMR5** | 定时 + 计数 + 捕获 + **三路 PWM** | IRQ_TMR3_VECTOR(16) / IRQ_TMR4_VECTOR(17) / IRQ_TMR5_VECTOR(18) |

计数时钟源统一为 `tmr_inc`（手册 §4.2 INCSRC=0 时选 TMR_INC；INCSRC=1 选外部引脚）。`tmr_inc` 的来源在工程中由 `CLKCON2` + `CLKCON0` 配置（见 §4 初始化原理）。

**手册 §4.2 关键结论**：`INCSEL[1:0]` 编码对递增时钟的含义——
- `00` = 系统时钟（Fsys ≈ 24MHz，**不是 tmr_inc**）
- `01` = 计数器输入**上升沿**（tmr_inc = x26m_div_clk = 1MHz，本工程约定）
- `10` = 下降沿
- `11` = 双边沿

这一约定是 §8 审查小节的关键依据。

### 1.2 本工程用途

| 通道 | 占用者 | 用途 | 来源 |
|---|---|---|---|
| **TMR0** | 系统 1ms tick | `timer0_init()` 周期 1ms 溢出中断，作为通用 `tick_cnt` | `interrupt.c` L28-53 |
| **TMR2** | 系统 1µs delay | `timer2_init()` 自由计数，`delay_us/_ms/_5ms`、`tick_get/tick_check_expire` 的基准；TMR1 精度测试用其 `TMR2CNT` 测时间 | `main.c` L100-106、L121-145 |
| **TMR1** | 空闲 → 测试 | `test_timer.c`：轮询模式精度测试 + 1000×1ms 溢出 ISR 测试 | `test/test_timer.c` |
| **TMR3** | 空闲 → PWM 测试 | `test_timer_pwm.c`：PWM0/PWM1/PWM2 → PB0/PB1/PB2，25% / 50% / 75% 占空比 | `test/test_timer_pwm.c` |
| TMR4 / TMR5 | 未使用 | 预留（手册 §4.3 同样支持 PWM/捕获/计数） | — |

### 1.3 涉及文件清单（相对工程根 `smart_mini/smart_mini/smart_mini/`）

| 相对路径 | 角色 |
|---|---|
| `main.c` | 入口：L228-241 时钟配置（CLKCON2 x26m_div_clk=1MHz、CLKCON0 tmr_inc=1MHz）、L100-106 `timer2_init()`、L242 `timer0_init()`、L274-279 测试入口 `#ifdef` 开关 |
| `interrupt.c` | L5-11 `register_isr`；L13-25 `cpu_low_irq_comm` 通用中断分发；L28-53 `timer0_isr` + `timer0_init` |
| `test/test_timer.c` | TMR1 轮询 + 中断测试（共 142 行） |
| `test/test_timer.h` | `void test_timer_run(void)` 声明 |
| `test/test_timer_pwm.c` | TMR3 三路 PWM 测试（共 99 行） |
| `test/test_timer_pwm.h` | `void test_timer_pwm_run(void)` 声明 |
| `test/test_common.h` | `TEST_LOG` 宏（L7） |
| `header/sfr.h` | TMR0/1/2 寄存器宏 L58-100；TMR3/4/5+PWM L546-570；FUNCMCON0/1/2 L44-46；GPIO* L436-461；CLKCON0/2 L63/280 |
| `header/int.h` | `IRQ_TMRx_VECTOR` 定义 L6-21（数值 3/4/5/16/17/18） |
| `header/include.h` | `TICK_1US/TICK_1MS/TICK_5MS` 宏 L7-9；`SYS_CLK = SYS_24M` L11-13 |

---

## 2. 涉及寄存器逐个说明

> 表中"地址"按 `SFRx_BASE + offset*4` 换算；SFR0_BASE=0x000、SFR1=0x100、…、SFR9=0x900（SFR 总基 0x00000000，`sfr.h` L19-35）。
> "sfr.h 行号"指 `header/sfr.h` 中宏定义的行号；手册章节号均来自 `docs/BT892X_UserManual_Driver.md`。

### 2.1 TMR0/1/2 寄存器组（手册 §4.2，L256-280）

| 寄存器 | 地址 | sfr.h 行号 | 关键位域 / 含义 | 手册章节 |
|---|---|---|---|---|
| `TMR0CON` | 0x050 | L58 | bit9 TPND（溢出挂起，R/写 1 清）/ bit7 TIE / bit6 INCSRC / bit3:2 INCSEL / bit0 TMREN | §4.2 第 256-262 行 |
| `TMR0CPND` | 0x054 | L59 | bit9 TPCLR：写 1 清 TPND（手册："TPND 只能靠写 CPND 清，写 CON 不清"） | §4.2 第 264-268 行 |
| `TMR0CNT` | 0x058 | L60 | [31:0] 32 位计数器；使能后递增，==PR 时溢出清零并置 TPND | §4.2 第 270-274 行 |
| `TMR0PR` | 0x05C | L61 | [31:0] 周期寄存器；**周期 = PR + 1** 个 tick | §4.2 第 276-280 行 |
| `TMR1CON` | 0x0D4 | L93 | 同 TMR0CON | §4.2 |
| `TMR1CPND` | 0x0D8 | L94 | 同 TMR0CPND | §4.2 |
| `TMR1CNT` | 0x0DC | L95 | 同 TMR0CNT | §4.2 |
| `TMR1PR` | 0x0E0 | L96 | 同 TMR0PR | §4.2 |
| `TMR2CON` | 0x0E8 | L97 | 同 TMR0CON | §4.2 |
| `TMR2CPND` | 0x0EC | L98 | 同 TMR0CPND | §4.2 |
| `TMR2CNT` | 0x0F0 | L99 | 同 TMR0CNT（工程作 1µs 自由计数基准） | §4.2 |
| `TMR2PR` | 0x0F4 | L100 | 同 TMR0PR（`timer2_init` 写 0xFFFFFFFF 防溢出） | §4.2 |

> **手册 §4.2 INCSEL 编码（关键，复用频繁）**：00=系统时钟 / 01=上升沿（tmr_inc）/ 10=下降沿 / 11=双边沿。
> **手册 §4.2 INCSRC**：0=TMR_INC（内部）/ 1=外部引脚。本工程所有调用都置 INCSRC=0。

### 2.2 TMR3/4/5 + PWM 寄存器组（手册 §4.3，L284-342）

| 寄存器 | 地址 | sfr.h 行号 | 关键位域 / 含义 | 手册章节 |
|---|---|---|---|---|
| `TMR3CON` | 0x900 | L546 | bit17 CPND / bit16 TPND / bit11 PWM2EN / bit10 PWM1EN / bit9 PWM0EN / bit8 CIE / bit7 TIE / bit6 INCSRC / bit5:4 CPTEDSEL / bit3:2 INCSEL / bit1 CPTEN / bit0 TMREN | §4.3 第 284-299 行 |
| `TMR3CPND` | 0x904 | L547 | bit17 CPCLR / bit16 TPCLR：写 1 清对应挂起 | §4.3 第 301-306 行 |
| `TMR3CNT` | 0x908 | L548 | [31:0] 计数器 | §4.3 第 308-312 行 |
| `TMR3PR` | 0x90C | L549 | [31:0] 周期 = PR + 1 | §4.3 第 314-318 行 |
| `TMR3CPT` | 0x910 | L550 | [31:0] 捕获值（只读） | §4.3 第 320-324 行 |
| `TMR3DUTY0` | 0x914 | L551 | [15:0] PWM0：低电平 = DUTY+1，高电平 = PR−DUTY | §4.3 第 326-330 行 |
| `TMR3DUTY1` | 0x918 | L552 | [15:0] PWM1：同上 | §4.3 第 332-336 行 |
| `TMR3DUTY2` | 0x91C | L553 | [15:0] PWM2：同上 | §4.3 第 338-342 行 |
| `TMR4CON` | 0x920 | L554 | 同 TMR3CON | §4.3 |
| `TMR4CPND` | 0x924 | L555 | 同 TMR3CPND | §4.3 |
| `TMR4CNT` | 0x928 | L556 | 同 TMR3CNT | §4.3 |
| `TMR4PR` | 0x92C | L557 | 同 TMR3PR | §4.3 |
| `TMR4CPT` | 0x930 | L558 | 同 TMR3CPT | §4.3 |
| `TMR4DUTY0` | 0x934 | L559 | 同 TMR3DUTY0 | §4.3 |
| `TMR4DUTY1` | 0x938 | L560 | 同 TMR3DUTY1 | §4.3 |
| `TMR4DUTY2` | 0x93C | L561 | 同 TMR3DUTY2 | §4.3 |
| `TMR5CON` | 0x940 | L563 | 同 TMR3CON | §4.3 |
| `TMR5CPND` | 0x944 | L564 | 同 TMR3CPND | §4.3 |
| `TMR5CNT` | 0x948 | L565 | 同 TMR3CNT | §4.3 |
| `TMR5PR` | 0x94C | L566 | 同 TMR3PR | §4.3 |
| `TMR5CPT` | 0x950 | L567 | 同 TMR3CPT | §4.3 |
| `TMR5DUTY0` | 0x954 | L568 | 同 TMR3DUTY0 | §4.3 |
| `TMR5DUTY1` | 0x958 | L569 | 同 TMR3DUTY1 | §4.3 |
| `TMR5DUTY2` | 0x95C | L570 | 同 TMR3DUTY2 | §4.3 |

### 2.3 引脚映射寄存器（手册 §3.3）

| 寄存器 | 地址 | sfr.h 行号 | 关键位域 / 含义 | 手册章节 |
|---|---|---|---|---|
| `FUNCMCON0` | 0x01C | L44 | [3:0] SD0MAP；[7:4] SPI0MAP；[11:8] UT0TXMAP；[15:12] UT0RXMAP；[19:16] HSTXMAP；[23:20] HSTRXMAP；[27:24] UT1TXMAP；[31:28] UT1RXMAP。**写入 0xF 释放映射** | §3.3 第 163-170 行 |
| `FUNCMCON1` | 0x020 | L45 | [3:0] FMOSCMAP；[7:4] UT2TXMAP；[11:8] UT2RXMAP；[15:12] SPI1MAP；[19:16] SPI1CSMAP | §3.3 第 174-178 行 |
| `FUNCMCON2` | 0x024 | L46 | [3:0] IISMAP；**[7:4] TMR3CPTMAP（捕获引脚映射）；[11:8] TMR3MAP（PWM-T3 Group 选择）**；[15:12] TMR4MAP；[19:16] TMR5MAP；[23:20] IR_MAP；[24:27] IIC_MAP；[31:28] DVPMAP。写入 0xF 清除映射 | §3.3 第 179-186 行 |
| `FUNCMCON3` | 0x0FC | L102 | [3:0] PDMMAP；[7:4] MPDMMAP | §3.3 第 188-193 行 |

### 2.4 PORTB GPIO 寄存器组（手册 §3.2）

| 寄存器 | 地址 | sfr.h 行号 | 含义 | 手册章节 |
|---|---|---|---|---|
| `GPIOB` | 0x648 | L438 | Port B 数据寄存器（读输入/写输出） | §3.2 第 145 行 |
| `GPIOBSET` | 0x640 | L436 | 写 1 置位对应位输出高 | §3.2 第 146 行 |
| `GPIOBCLR` | 0x644 | L437 | 写 1 清对应位输出低 | §3.2 第 147 行 |
| `GPIOBDIR` | 0x64C | L439 | 0=输出，1=输入（默认 0xFF） | §3.2 第 148 行 |
| `GPIOBDE` | 0x650 | L440 | 0=模拟 IO，1=数字 IO（默认 0xFF） | §3.2 第 155 行 |
| `GPIOBFEN` | 0x654 | L441 | 0=GPIO，1=功能 IO（默认 0xFF） | §3.2 第 156 行 |
| `GPIOBPU`/`GPIOBPD` | 0x658/0x65C | L443/L444 | 10KΩ 上下拉控制 | §3.2 |

### 2.5 时钟与电源相关寄存器

| 寄存器 | 地址 | sfr.h 行号 | 含义 | 手册章节 |
|---|---|---|---|---|
| `CLKCON0` | 0x064 | L63 | bit4:5:6 系统时钟选择等；**bit24 tmr_inc 选择位（工程注释：`tmr_inc select x26m_div_clk = 1M`）**；bit23:25 | §3.3 节未详细展开（手册 §10 缺失） |
| `CLKCON1` | 0x074 | L67 | SPLL/UART 时钟微调 | 同上 |
| `CLKCON2` | 0x3A8 | L280 | **[31:24] x26m_div_clk 分频系数 N（分频 = N+1）**；[7:0]/[15:8]/[23:16] 其他 | §3.3 节未详细展开（手册 §10 缺失） |
| `RTCCON3` | 0x9CC | L598 | bit0 VDDBT enable（核心 1.2V） | §5 |
| `PWRCON0` | 0x3D4 | L266 | bit20 PMU normal | §5/§9 |

### 2.6 中断控制寄存器（手册 §2.3）

| 寄存器 | 地址 | sfr.h 行号 | 关键位域 / 含义 | 手册章节 |
|---|---|---|---|---|
| `PICCON` | 0x440 | L323 | bit0 GIE 全局中断使能；bit1 LPINTEN；bit2 HPINTEN；bit16 GIEM | §2.3 第 64-73 行 |
| `PICEN` | 0x444 | L324 | [31:0] 中断使能位；写 1 使能对应中断向量 | §2.3 第 94-97 行 |
| `PICPR` | 0x448 | L325 | [31:0] 优先级选择：0=低，1=高 | §2.3 第 106-109 行 |
| `PICPR1` | 0x524 | L349 | [31:0] 与 PICPR 组合实现 4 档优先级 | §2.3 第 111-115 行 |
| `PICADR` | 0x44C | L326 | [31:8] 中断入口基址（main.c L244 写 `&__comm_vma`） | §2.3 第 118-122 行 |
| `PICPND` | 0x450 | L327 | [31:0] 中断挂起（仅读，由 cpu_low_irq_comm 扫描） | §2.3 第 124-130 行 |

---

## 3. 引脚定义与复用

### 3.1 PWM-T3 引脚速查（手册 §3.3 / pinfunction §5.5 第 244-246 行）

| PWM 通道 | Group | 可分配 PAD（pinfunction §5.5） | 工程实际选用 |
|---|---|---|---|
| **PWM0-T3** | G1 | PB0, PB1, PB2, PB3, PE0, PE4, PF0 | **PB0**（pinfunction §4.2 第 114 行 PWM0-T3-G1） |
| **PWM0-T3** | G2 | PB3, PB4, PB5（pinfunction §4.2 列） | — |
| **PWM0-T3** | G3 | PA3, PA4（pinfunction §4.1） / PF0 | — |
| **PWM0-T3** | G4 | PE0, PE4 | — |
| **PWM1-T3** | G1 | PA3, PA4, PB1, PB2, PB3, PE4（速查表 §5.5 第 245 行） | **PB1**（§4.2 第 115 行 PWM1-T3-G1） |
| **PWM2-T3** | G1 | PA4, PB2, PB5, PE0（速查表 §5.5 第 246 行） | **PB2**（§4.2 第 116 行 PWM2-T3-G1） |

### 3.2 工程实际占用映射（test_timer_pwm.c 使用的引脚）

| 引脚 | PAD 名 | PWM 通道 | Group | 手册/引脚文档出处 |
|---|---|---|---|---|
| **PB0/WK1** | PWM0-T3-G1 | PWM0 | G1 | pinfunction §4.2 第 114 行 `PWM0-T3-G1`；同时是 TMR3CAP_G3 / IR_G3 / SDCMD-G2 / IIC_DAT-G4 |
| **PB1/WK2** | PWM1-T3-G1 | PWM1 | G1 | pinfunction §4.2 第 115 行 `PWM1-T3-G1`；同时是 RX0-G2 / RX2-G2 / TMR3CAP_G4 |
| **PB2/WK3** | PWM2-T3-G1 | PWM2 | G1 | pinfunction §4.2 第 116 行 `PWM2-T3-G1`；同时是 TX0-G2 / TX2-G2 / SDDAT0-G2 |

### 3.3 FUNCMCONx 映射值与冲突约束

| 位域 | 含义 | PWM 测试写入值 | 释义 |
|---|---|---|---|
| `FUNCMCON0[3:0]` | SD0MAP | **`\|= 0xF`**（test_timer_pwm.c L36） | 清除 SD0 映射（防止 PB0 被 SDCMD-G2 抢占，pinfunction §4.2 第 114 行 `SDCMD-G2` 列） |
| `FUNCMCON2[11:8]` | **TMR3MAP** | `= 0x1`（L38，掩码 `(0x1 << 8)`） | 选 G1 → PB0/PB1/PB2 |
| `FUNCMCON2[7:4]` | **TMR3CPTMAP** | `= 0xF`（L38，掩码 `(0xF << 4)`） | 清除 TMR3 捕获引脚映射（防止 PB0 被误绑为 TMR3CAP_G3，pinfunction §4.2 第 114 行 `TMR3CAP_G3/IR_G3`） |

### 3.4 与其他外设的引脚冲突约束

> 同一物理 PAD 同时挂在多个映射位域时，**先激活的映射占用**，需在 `FUNCMCONx` 中显式 `= 0xF` 释放另一映射。`test_timer_pwm.c` L31-35 注释明确警告了三处潜在冲突：

| 冲突源 | 占用 PBx 的功能 | 解决方式 | 出处 |
|---|---|---|---|
| **SD0** | PB0=SDCMD-G2（pinfunction §4.2 第 114 行）、PB1=SDCLK-G2（第 115 行）、PB2=SDDAT0-G2（第 116 行） | `FUNCMCON0 \|= 0xF`（test_timer_pwm.c L36）清除 SD0MAP | pinfunction §4.2 |
| **TMR3CAP** | PB0=TMR3CAP_G3（第 114 行）、PB1=TMR3CAP_G4（第 115 行） | `FUNCMCON2[7:4]=0xF` 清 TMR3CPTMAP（test_timer_pwm.c L38） | pinfunction §4.2 |
| **UART0/2** | PB1=RX0-G2/RX2-G2、PB2=TX0-G2/TX2-G2 | 物理互斥：`TEST_TIMER_PWM_EN` 与 `TEST_UART_EN` / `TEST_UART_RECV_EN` 等只能开一个（main.c L52-54 注释） | main.c L52 |
| **IIC** | PB0=IIC_DAT-G4、PB1=IIC_CLK-G3/G4、PB2=IIC_DAT-G3（pinfunction §4.2） | 同上，IIC 测试与 PWM 测试物理互斥 | main.c L52 |
| **唤醒源** | PB0=WK1、PB1=WK2、PB2=WK3（pinfunction §4.2 PAD 名后缀） | 复用为 PWM 输出后会失去 wakeup 能力 | pinfunction 附录 A.4 |
| **PB3**（USBDP） | USB 升级口 + TX0-G3 + PWM0-T3-G2 | 不参与本测试，但工程默认 UART0 用 PB3（G3）走 USB print（main.c L165-180 `uart0_mapping_sel`） | pinfunction §4.2 第 117 行 |

---

## 4. 初始化原理

### 4.1 时钟源（causal chain）

**目标**：让所有定时器（基础 TMR0/1/2 + 多功能 TMR3/4/5）在同一基准下计数，便于互测、统一 `delay_*`。

**链路**（main.c L228-241 + pinfunction/手册 §10 缺失的时钟树细节）：

```
XOSC26M (26 MHz 晶振)
    │
    ▼  CLKCON2 [31:24] = 25   →  分频系数 N+1 = 26
x26m_div_clk = 26 MHz / 26 = 1 MHz
    │
    ▼  CLKCON0 bit24 = 1      →  选 x26m_div_clk
tmr_inc = x26m_div_clk = 1 MHz
    │
    ▼  TMRxCON: INCSRC=0, INCSEL=01(bit2)   →  TMRx 选 tmr_inc 上升沿
TMR0/1/2/3 计数步长 = 1 µs
```

> 对照手册 §4.2：INCSEL=01 才是 "计数器输入上升沿"，即 `tmr_inc` 的 1 MHz。INCSEL=00 是系统时钟（24MHz，**不是 1MHz**）。

**main.c 关键行**：
- L231：`CLKCON2 &= 0x00ffffff; CLKCON2 |= (25 << 24);` → 写 25 至 [31:24]（注释："configure x26m_div_clk = 1M (timer, ir, fmam use)"）
- L232-233：`CLKCON0 &= ~(7 << 23); CLKCON0 |= BIT(24);` → 选 x26m_div_clk 为 tmr_inc（注释："tmr_inc select x26m_div_clk = 1M"）

### 4.2 复用（causal chain）

**目标**：让 PB0/PB1/PB2 既保持 GPIO 默认 Hiz，又能从内部 Timer3 拉出 PWM 波形。

**链路**：
```
PAD PBx 复位后 → Hiz / GPIO 模式（GPIOBFEN=0）
    │
    ▼  GPIOBFEN |= BIT(x)      →  进入 "功能 IO" 模式（手册 §3.2 第 156 行）
    ▼  GPIOBDE  |= BIT(x)      →  数字 IO
    ▼  GPIOBDIR &= ~BIT(x)     →  输出方向
    │
    ▼  FUNCMCON2[11:8] = 0x1   →  TMR3 选 Group 1（手册 §3.3 第 185 行）
    ▼  FUNCMCON2[7:4]  = 0xF   →  清捕获映射防冲突（test_timer_pwm.c L38）
    ▼  FUNCMCON0[3:0]  |= 0xF  →  清 SD0 映射防 PB0 被占（test_timer_pwm.c L36）
    │
    ▼  TMR3CON = PWM2EN | PWM1EN | PWM0EN | INCSEL=00/01 | TMREN
PWM 波形输出至 PB0/PB1/PB2
```

**因果**：
1. `GPIOBFEN` 不开 → PAD 被锁在 GPIO 模式，PWM 信号即使在硬件层生成也到不了 PAD。
2. `FUNCMCON2[11:8]` 不配 → TMR3 不知道把三路 PWM 路由到哪组引脚，默认 0 = 无映射。
3. `FUNCMCON0[3:0]` 不清 0xF → 上电默认 SD0MAP=0（手册 §3.3 第 170 行 default 0x0）会占用 PB0=SDCMD-G2。
4. `FUNCMCON2[7:4]` 不清 0xF → TMR3CAP 可能把 PB0 当作捕获输入。

### 4.3 中断（causal chain）

**目标**：TMR0 1ms 周期性 ISR + TMR1 测试 ISR。

**链路**（interrupt.c L28-53）：

```
1. tbl_irq_vector[IRQ_TMR0_VECTOR] = timer0_isr   ← register_isr 写入向量表
2. TMR0CON = BIT(7)                               ← 先开 TIE (L47)
3. TMR0CNT = 0;  TMR0PR = 999                     ← 1ms 周期
4. TMR0CON |= BIT(2) | BIT(0)                     ← INCSEL + TMREN 启动 (L50)
5. PICPR  &= ~BIT(IRQ_TMR0_VECTOR)                ← 清 0 = 高优先级 (L51)
6. PICEN  |=  BIT(IRQ_TMR0_VECTOR)                ← 写 1 使能向量 (L52)
7. PICCON |= 0x10003                              ← GIE+LPINTEN+GIEM (main.c L245)
8. 中断到达 → cpu_low_irq_comm 扫 PICPND → 查表 → 调 timer0_isr
```

**因果**：
1. 必须先注册 ISR 再使能向量（否则 PICEN 一置 1，第一次溢出将跳到 0）。
2. 必须先清 TPND 再开中断（否则历史溢出挂起会触发伪中断——见 §7.3 关键 bug 修复）。
3. 必须先开 GIE（main.c L245）再进入测试，否则中断向量无法被响应。
4. TMR0 ISR 内**第一件事**必须写 `TMR0CPND = BIT(9)` 清挂起（手册 §4.2 第 264-268 行：TPND 只能靠 CPND 写 1 清）。

---

## 5. 初始化操作步骤

> **约定**：以下步骤按时间顺序排列；其中步骤 1（时钟）已在 `main()` 中无条件执行，步骤 2-4 在每个测试的 `*_init` 函数内执行。

### Step 1 — 时钟门控与电源（main.c L224-237）

```
1.1 WDT_DIS()                            ; 关看门狗
1.2 usb_disable();  sd_disable()         ; 关 USB/SD 时钟与映射（L147-163）
1.3 LVDCON &= ~BIT(30)                   ; LVD 不复位
1.4 FUNCMCON0 = 0xff000000               ; 关闭 UART1 默认映射（L228）
1.5 FUNCMCON1 = 0xffffffff               ; 关闭 UART2 等默认映射
1.6 CLKCON2 &= 0x00ffffff;               ; 清旧分频
     CLKCON2 |= (25 << 24);              ; x26m_div_clk = 26MHz/26 = 1MHz
1.7 CLKCON0 &= ~(7 << 23);               ; 清旧 tmr_inc 选择
     CLKCON0 |= BIT(24);                 ; tmr_inc ← x26m_div_clk (1MHz)
1.8 timer2_init()                        ; 启动 TMR2 自由计数
1.9 PWRCON0 |= BIT(20);  RTCCON3 |= BIT(0);  ; PMU normal + VDDBT enable
1.10 uart0_mapping_sel()                 ; UART0 切到 PB3（G3）作 printf
```

> **手册出处**：`CLKCON2/CLKCON0` 位的精确含义手册 §3.3 未列出（手册 §10 已声明缺失时钟树）。工程注释即权威。

### Step 2 — 系统 tick 定时器（TMR0）

```
2.1 register_isr(IRQ_TMR0_VECTOR, timer0_isr);   ; interrupt.c L46
2.2 TMR0CPND = BIT(9);                           ; 先清挂起（好习惯）
2.3 TMR0CON = BIT(7);                            ; TIE=1（先开中断，避免伪中断）
2.4 TMR0CNT = 0;                                 ; 计数清零
2.5 TMR0PR = 1000 - 1;                           ; 周期 1000 个 tmr_inc = 1ms
2.6 TMR0CON |= BIT(2) | BIT(0);                  ; INCSEL=01 + TMREN=1 启动
2.7 PICPR &= ~BIT(IRQ_TMR0_VECTOR);              ; 高优先级（清 0=高）
2.8 PICEN |=  BIT(IRQ_TMR0_VECTOR);              ; 写 1 使能向量
2.9 （main.c L245 已开 GIE+LPINTEN+GIEM）
```

### Step 3 — 自由计数定时器（TMR2，1µs delay tick）

```
3.1 TMR2CON = 0;                                 ; 停表清配置（main.c L102）
3.2 TMR2PR  = 0xFFFFFFFFul;                      ; 大周期避免溢出（L103）
3.3 TMR2CNT = 0;                                 ; 清零（L104）
3.4 TMR2CON |= BIT(2) | BIT(0);                  ; INCSEL=01 + TMREN 启动（L105）
```

### Step 4 — TMR1 轮询测量（test_timer.c `test_timer1_measure_poll` L30-68）

```
4.1 TMR1CPND = BIT(9);                           ; 先清历史溢出挂起（L35）
4.2 TMR1CON = 0;                                 ; 停表（L38）
4.3 TMR1CNT = 0;                                 ; 清零（L39）
4.4 TMR1PR  = expected_us - 1;                   ; 周期 = expected_us µs（L40）
4.5 t0 = TMR2CNT;                                ; 取 TMR2 当前 µs tick 作起点
4.6 TMR1CON = BIT(2) | BIT(0);                   ; INCSEL=01 + TMREN 启动（L46）
4.7 while ((TMR1CON & BIT(9)) == 0);             ; 等 TPND=1（溢出）
4.8 t1 = TMR2CNT;  TMR1CON = 0;                  ; 终点 + 停表
4.9 return t1 - t0;                              ; 实测耗时（µs）
```

### Step 5 — TMR1 中断测试（test_timer.c Part 2 L97-136）

```
5.1 register_isr(IRQ_TMR1_VECTOR, test_timer1_isr);  ; 注册 ISR（L100）
5.2 g_t1_isr_count = 0;                              ; 清计数（L101）
5.3 TMR1CPND = BIT(9);                               ; 【关键】清 Part 1 残留挂起（L106）
5.4 TMR1CON = BIT(7);                                ; TIE=1（L109）
5.5 TMR1CNT = 0;
5.6 TMR1PR  = 1000 - 1;                              ; 1ms 周期
5.7 TMR1CON |= BIT(2) | BIT(0);                      ; 启动（L112）
5.8 PICPR  &= ~BIT(IRQ_TMR1_VECTOR);                 ; 高优先级（L113）
5.9 PICEN  |=  BIT(IRQ_TMR1_VECTOR);                 ; 使能向量（L114）
5.10 循环等 g_t1_isr_count == 1000（带超时 2s）
5.11 TMR1CON = 0;  PICEN &= ~BIT(IRQ_TMR1_VECTOR);   ; 关闭（L129-130）
```

### Step 6 — TMR3 PWM 输出（test_timer_pwm.c L23-99）

```
6.1 FUNCMCON0 |= 0xF;                            ; 清 SD0MAP 释放 PB0（L36）
6.2 FUNCMCON2 &= ~((0xF << 4) | (0xF << 8));     ; 清 TMR3CPTMAP + TMR3MAP（L37）
6.3 FUNCMCON2 |=  (0xF << 4) | (0x1 << 8);       ; TMR3CPTMAP=0xF, TMR3MAP=0x1（G1）（L38）
6.4 GPIOBFEN |= BIT(0)|BIT(1)|BIT(2);            ; 功能 IO（L41）
6.5 GPIOBDE  |= BIT(0)|BIT(1)|BIT(2);            ; 数字 IO（L42）
6.6 GPIOBDIR &= ~(BIT(0)|BIT(1)|BIT(2));         ; 输出方向（L43）
6.7 TMR3CNT = 0;  TMR3PR = 999;                  ; 1kHz 周期（L52-53）
6.8 TMR3DUTY0 = 749;  TMR3DUTY1 = 499;  TMR3DUTY2 = 249;   ; 25/50/75%（L62-64）
6.9 TMR3CON = BIT(11)|BIT(10)|BIT(9)|(0<<2)|BIT(0); ; PWM2EN|PWM1EN|PWM0EN|INCSEL=00|TMREN（L71-75）
```

> **审查小节关注点**：步骤 6.9 使用 INCSEL=00，但其他定时器（步骤 2-5）均用 INCSEL=01。详见 §8。

---

## 6. 代码详解

### 6.1 `interrupt.c` L28-53 — TMR0 系统 tick ISR 与初始化

```c
27  //timer0 1ms interrupt
28  AT(.com_text.isr)
29  void timer0_isr(void)
30  {
31      static uint tick_cnt = 0;
32      TMR0CPND = BIT(9);              //Clear Pending        ← 手册 §4.2：TPND 只能靠 CPND 写 1 清
33      tick_cnt++;
34
35      if ((tick_cnt % 5) == 0) {      //5ms
36
37      }
38
39      if ((tick_cnt % 1000) == 0) {   //1s
40          tick_cnt = 0;
41      }
42  }
43
44  void timer0_init(void)
45  {
46      register_isr(IRQ_TMR0_VECTOR, timer0_isr);
47      TMR0CON =  BIT(7); //TIE                         ← 先开 TIE（手册 §4.2 bit7）
48      TMR0CNT = 0;
49      TMR0PR  = 1000 - 1;         //1ms interrupt       ← PR=999 → 周期 = PR+1 = 1000 µs
50      TMR0CON |= BIT(2) | BIT(0); // EN                  ← INCSEL=01 (bit2) + TMREN (bit0)
51      PICPR &= ~BIT(IRQ_TMR0_VECTOR);                   ← 手册 §2.3：清 PICPR[x]=0 = 高优先级
52      PICEN |=  BIT(IRQ_TMR0_VECTOR);                   ← 手册 §2.3：PICEN[x]=1 使能该向量
53  }
```

**逐段解释**：
- **L32 `TMR0CPND = BIT(9)`**：手册 §4.2 第 264-268 行明文 "TPCLR 写 1 清溢出挂起"，**写 TMR0CON 不清 TPND**。本 ISR 不写 TPND 将导致下一次进入 ISR 仍然看到旧挂起。
- **L47 先写 `BIT(7)` 再 L50 `|= BIT(2)|BIT(0)`**：分两步是经典写法——先单独配置 TIE，避免与其他位竞争；然后再 `|=` 启动位，保留 TIE=1。
- **L50 `BIT(2)|BIT(0)`**：`BIT(2)=INCSEL 的 bit0`（INCSEL=01 = tmr_inc 上升沿，手册 §4.2 第 261 行）；`BIT(0)=TMREN`（手册 §4.2 第 262 行）。
- **L49 `1000-1`**：手册 §4.2 第 280 行 "周期 = TMRPR + 1"，所以 1ms@1MHz 需要 PR=999。
- **L51 `PICPR &= ~BIT(...)`**：手册 §2.3 第 106-109 行 PICPR[x]=0 选低优先级；&= ~ 是为了**保留其他位**（不暴力写 0）。
- **L52 `PICEN |= BIT(...)`**：手册 §2.3 第 94-97 行 PICEN[x]=1 使能对应中断向量。

### 6.2 `main.c` L100-106 — TMR2 自由计数

```c
99   //timer2 for delay function
100  void timer2_init(void)
101  {
102      TMR2CON = 0;                                            //select tmr_inc rising edge
103      TMR2PR = 0xfffffffful;
104      TMR2CNT = 0;
105      TMR2CON |= BIT(2) | BIT(0);                             //TMR2 Start
106  }
```

**逐段解释**：
- **L102 `TMR2CON = 0`**：停表清配置（手册 §4.2 bit0=0 关 TMREN）。
- **L103 `TMR2PR = 0xFFFFFFFF`**：32 位定时器最大周期 = 0xFFFFFFFF + 1 = 2^32 µs ≈ 71.6 分钟。`delay_us/_ms/_5ms` 永远不会跨越溢出边界。
- **L105 `BIT(2) | BIT(0)`**：与 §6.1 同义——选 tmr_inc 上升沿 + 启动。
- L102 注释 "select tmr_inc rising edge" 是**写代码时刻意**的（虽然 L102 实际只清 CON 寄存器，真正选择 INCSEL 是 L105）。

### 6.3 `test_timer.c` L30-68 — TMR1 轮询精度测量

```c
30  static u32 test_timer1_measure_poll(u32 expected_us)
31  {
32      u32 t0, t1;
33
34      // 【关键】先清溢出挂起位（否则上次的 BIT(9)=1 会让 while 立即跳过）
35      TMR1CPND = BIT(9);
36
37      // 1. 停止 TMR1，清零计数
38      TMR1CON = 0;
39      TMR1CNT = 0;
40      TMR1PR  = expected_us - 1;     // 周期 = PR + 1 个 tmr_inc（1µs）
41
42      // 2. 记录 TMR2 当前 tick（开始）
43      t0 = TMR2CNT;
44
45      // 3. 启动 TMR1（BIT(2)=INCSEL → tmr_inc，BIT(0)=TMREN）
46      TMR1CON = BIT(2) | BIT(0);
47
48      // 4. 等待溢出（TPND = bit9 置 1），带超时保护
49      {
50          u32 timeout = t0 + TMR1_POLL_TIMEOUT_US;
51          while ((TMR1CON & BIT(9)) == 0) {
52              if ((u32)(TMR2CNT - timeout) < 0x80000000ul) {
53                  TEST_LOG("  [TIMEOUT] TMR1 overflow not detected");
54                  TMR1CON = 0;
55                  return 0;
56              }
57          }
58      }
59
60      // 5. 记录 TMR2 当前 tick（结束）
61      t1 = TMR2CNT;
62
63      // 6. 停止 TMR1
64      TMR1CON = 0;
65
66      // 7. 返回实际耗时（µs）
67      return t1 - t0;
68  }
```

**逐段解释**：
- **L35 `TMR1CPND = BIT(9)`**：**进入测量前必须清**，因为前一次测量残留的 TPND=1 会让 L51 while 立即通过，测量返回 0。
- **L38 `TMR1CON = 0`**：停表并清所有配置（包括 TIE 防止意外 ISR）。
- **L40 `expected_us - 1`**：周期 = PR+1 = expected_us 个 tick，1 tick = 1µs（因为 INCSEL=01 选 tmr_inc=1MHz）。
- **L43 `t0 = TMR2CNT`**：用另一个 1µs tick 自由计数定时器 TMR2 作外部基准，避免使用系统 `delay_*`（依赖 TMR2 自身会形成鸡生蛋）。
- **L46 `BIT(2) | BIT(0)`**：INCSEL=01 + TMREN。
- **L51 `while ((TMR1CON & BIT(9)) == 0)`**：手册 §4.2 第 258 行 TPND 位定义——溢出后硬件置 1。
- **L52 无符号回环减法超时判断**：`(u32)(TMR2CNT - timeout) < 0x80000000ul` 等价于 `TMR2CNT > timeout`（无符号），避免 TMR2CNT 回绕时符号比较失效。
- **L67 `return t1 - t0`**：实测 µs 数（误差 = 中断进入/退出延迟 + 上下文保存开销，**对轮询路径为 0**，因为没进 ISR）。

### 6.4 `test_timer.c` L97-136 — TMR1 中断测试（关键 bug 修复）

```c
97   // ===== Part 2: 中断模式触发测试 =====
98   TEST_LOG("[Part 2] Interrupt mode - 1000 x 1ms ISR");
99
100  register_isr(IRQ_TMR1_VECTOR, test_timer1_isr);
101  g_t1_isr_count = 0;
102
103  // 【关键】清除 Part 1 轮询遗留的溢出挂起 TPND(bit9)
104  // 否则 TIE+PICEN 使能瞬间会立刻触发一次伪中断，使 1000 次计数提前约 1ms 完成
105  // 手册 §4.2：TPND 只能通过 TMR1CPND[9] TPCLR 写 1 清除（写 TMR1CON 不清）
106  TMR1CPND = BIT(9);
107
108  // 配置 TMR1 为 1ms 周期中断
109  TMR1CON = BIT(7);                  // TIE = 1（先开中断）
110  TMR1CNT = 0;
111  TMR1PR  = 1000 - 1;                // 1ms 周期
112  TMR1CON |= BIT(2) | BIT(0);        // tmr_inc + 启动
113  PICPR  &= ~BIT(IRQ_TMR1_VECTOR);   // 优先级（清 0 = 高优先级，按手册）
114  PICEN  |=  BIT(IRQ_TMR1_VECTOR);   // 使能 TMR1 向量
115
116  // 等待 1000 次中断
117  {
118      u32 t0 = TMR2CNT;
119      u32 timeout = t0 + 2000000;    // 2 秒超时
120      while (g_t1_isr_count < 1000) {
121          if ((u32)(TMR2CNT - timeout) < 0x80000000ul) {
122              TEST_LOG("  [TIMEOUT] ISR not firing (count=%u)", (u32)g_t1_isr_count);
123              break;
124          }
125      }
126      u32 t1 = TMR2CNT;
127
128      // 关闭 TMR1 和中断向量
129      TMR1CON = 0;
130      PICEN  &= ~BIT(IRQ_TMR1_VECTOR);
131
132      measured = t1 - t0;
133      err = (int)measured - 1000000;
134      TEST_LOG("  1000 ISR: expected 1000000us, measured %u us, err=%d us, count=%u",
135               (u32)measured, err, (u32)g_t1_isr_count);
136  }
```

**逐段解释**：
- **L106 `TMR1CPND = BIT(9)`**：**修复点**。Part 1 末尾 TMR1 溢出但未清 TPND，进入 Part 2 时 `TMR1CON = BIT(7)` 只置 TIE 不清 TPND（手册 §4.2 第 258 行：TPND 写 CON 不清），加上 L114 一使能向量 → 立即触发伪 ISR → 1000 次提前 ~1ms 完成，err≈−979µs。
- **L109 `BIT(7)` 然后 L112 `|=`**：分两步同 §6.1 解释。
- **L113 `PICPR &= ~BIT(...)`**：高优先级（手册 §2.3）。
- **L114 `PICEN |= BIT(...)`**：使能向量后，**第一次 TMR1 溢出将进入 ISR**（前提是 L106 已清 TPND）。
- **L20-25 ISR**：`TMR1CPND = BIT(9); g_t1_isr_count++;` —— 注意 ISR 内第一件事就是清挂起，否则 PICPND 一直为 1 会反复进入 ISR。

### 6.5 `test_timer_pwm.c` L23-99 — TMR3 三路 PWM

```c
23  void test_timer_pwm_run(void)
24  {
25      printf("\n===== BT892X Timer3 PWM Test =====\n\n");
26
27      // ================================================================
28      // 1. 配置 PWM 引脚映射: TMR3MAP = G1 (FUNCMCON2[11:8] = 0x1)
29      //    PWM0 → PB0, PWM1 → PB1, PWM2 → PB2
30      //
31      //    ★ 必须先清除冲突映射:
32      //       - SD0MAP=0xF: main.c 中 uart0_mapping_sel() 的 = 赋值把它清零了
33      //         如果 SD0MAP=0 可能默认占用 PB0(SDCMD-G2)
34      //       - TMR3CPTMAP=0xF: 防止 PB0 被误映射为捕获输入(TMR3CAP_G3)
35      // ================================================================
36      FUNCMCON0 |= 0xF;                           // SD0MAP = 0xF (clear, 释放 PB0)
37      FUNCMCON2 &= ~((0xF << 4) | (0xF << 8));    // 清除 TMR3CPTMAP + TMR3MAP
38      FUNCMCON2 |= (0xF << 4) | (0x1 << 8);       // TMR3CPTMAP=clear, TMR3MAP=G1
39
40      // PB0/PB1/PB2 → 功能 IO 模式 (PWM 输出)
41      GPIOBFEN |= BIT(0) | BIT(1) | BIT(2);   // 功能 IO 模式
42      GPIOBDE  |= BIT(0) | BIT(1) | BIT(2);   // 数字 IO
43      GPIOBDIR &= ~(BIT(0) | BIT(1) | BIT(2)); // 输出方向
44
45      // ================================================================
46      // 2. 配置 Timer3 参数
47      //    时钟源: tmr_inc = 1MHz (与 main.c 一致)
48      //    PWM 频率: 1KHz (周期 = 1000 个 tick)
49      // ================================================================
50      u32 pr_val = 1000 - 1;          // PR = 999, 周期 = 1000 tick = 1ms
51
52      TMR3CNT = 0;
53      TMR3PR  = pr_val;
54
55      // 占空比计算 (手册公式):
56      //   高电平长度 = PR - DUTY
57      //   DUTY = PR - (PR+1) * 占空比(高)
58      //
59      //   PWM0: 25% → 高电平 250 tick → DUTY0 = 999 - 250 = 749
60      //   PWM1: 50% → 高电平 500 tick → DUTY1 = 999 - 500 = 499
61      //   PWM2: 75% → 高电平 750 tick → DUTY2 = 999 - 750 = 249
62      TMR3DUTY0 = 749;    // PWM0: 25% 高电平占空比
63      TMR3DUTY1 = 499;    // PWM1: 50% 高电平占空比
64      TMR3DUTY2 = 249;    // PWM2: 75% 高电平占空比
65
66      // ================================================================
67      // 3. 启动 Timer3 PWM
68      //    PWM0EN + PWM1EN + PWM2EN + TMREN
69      //    INCSEL=00 (tmr_inc 时钟), INCSRC=0 (内部时钟)
70      // ================================================================
71      TMR3CON = BIT(11)          // PWM2EN
72              | BIT(10)          // PWM1EN
73              | BIT(9)           // PWM0EN
74              | (0 << 2)         // INCSEL=00: tmr_inc
75              | BIT(0);          // TMREN: 使能定时器
76
77      // ================================================================
78      // 4. 打印配置信息
79      // ================================================================
80      printf("Timer3 PWM Configuration:\n");
81      printf("  Clock source : tmr_inc = 1MHz\n");
82      printf("  TMR3PR       = %lu (period = %lu tick = 1ms, f = 1KHz)\n",
83             pr_val, pr_val + 1);
84      ...
99  }
```

**逐段解释**：
- **L36 `FUNCMCON0 |= 0xF`**：|= 是为了只动 [3:0] 不影响 UART0 的高位。但**风险**：如果 main.c 之前已把 SD0MAP 配成别的非 0xF 值，|= 0xF 会保留那些位（|= 不会清零）。此处实际效果是写入 0xF（因为 SD0MAP 之前未动过，初始值=0）。注释声称 "释放 PB0"。
- **L37 `FUNCMCON2 &= ~((0xF<<4)|(0xF<<8))`**：同时清 [7:4] TMR3CPTMAP 和 [11:8] TMR3MAP，复位到默认 0（手册 §3.3 第 185-186 行 default 0x0）。
- **L38 `FUNCMCON2 |= (0xF<<4)|(0x1<<8)`**：TMR3CPTMAP 写 0xF（清除），TMR3MAP 写 0x1（G1）。**注意**：手册第 186 行 `TMR3CPTMAP 0001~0111=G1~G7, 1111=清除`，因此 0xF 是合法"清除"编码。
- **L41-43 GPIO 配置**：手册 §3.2 第 148/155/156 行标准序列——FEN 选功能、DE 选数字、DIR 选输出。
- **L52-53 `TMR3CNT=0; TMR3PR=999`**：手册 §4.3 第 308-318 行；1ms@1MHz 时 PR=999。
- **L62-64 占空比计算**：**反向思维**——手册 §4.3 第 330/336/342 行公式：低电平 = DUTY+1，高电平 = PR−DUTY。要得到 25% 占空比（高电平 250 tick），DUTY = PR−250 = 749。
- **L71-75 TMR3CON 写入**：
  - `BIT(11)` = PWM2EN（手册 §4.3 第 290 行）
  - `BIT(10)` = PWM1EN（第 291 行）
  - `BIT(9)` = PWM0EN（第 292 行）
  - `(0<<2)` = INCSEL=00（**注释声称 tmr_inc，但按手册 §4.2 实际是系统时钟 24MHz**——见 §8 审查小节）
  - `BIT(0)` = TMREN（第 299 行）
- **L95-98 打印心跳**：每 2 秒打印一次 `TMR3CNT`，证明定时器活着。

---

## 7. 测试步骤与预期现象

### 7.1 TMR1 精度 + 中断（`TEST_TIMER_EN`）

**接线**：仅需 USB-TTL 连 PB3（UART0 TX，G3）@1.5Mbps（G3，main.c L179 FUNCMCON0=`(7<<12)|(3<<8)`），串口接 PC 看打印。无需外部连接。

**main.c 开关**（L51）：
```c
// #define TEST_GPIO_EN    1
#define TEST_TIMER_EN   1
```

**串口输出预期**（来自 docs/test_timer.md §2.4 实测）：
```
[TEST] ========================================
[TEST] Timer1 test start (tmr_inc = 1MHz)
[TEST] ========================================
[TEST] [Part 1] Polling mode - period accuracy
[TEST]   1ms:   expected 1000us,   measured 1000 us,    err=0 us
[TEST]   10ms:  expected 10000us,  measured 10000 us,   err=0 us
[TEST]   100ms: expected 100000us, measured 100000 us,  err=0 us
[TEST] [Part 2] Interrupt mode - 1000 x 1ms ISR
[TEST]   1000 ISR: expected 1000000us, measured 1000021 us, err=21 us, count=1000
[TEST] ========================================
[TEST] Timer1 test done
[TEST] ========================================
```

**判断标准**：
- Part 1 三档 err=0 µs（轮询无 ISR 开销）。
- Part 2 err≈±几十 µs（ISR 进入/退出开销，正常），count=1000。
- 如果 Part 2 出现 err≈−1000µs（少一个周期），即 §7.3 描述的"伪中断"bug，需检查是否漏写 `TMR1CPND = BIT(9)`。

### 7.2 TMR3 PWM（`TEST_TIMER_PWM_EN`）

**接线**（docs/test_timer.md §3.1）：

| 逻辑分析仪通道 | BT892X 引脚 | PWM 输出 |
|---|---|---|
| CH1 | PB0 | PWM0-T3-G1，25% 占空比 |
| CH2 | PB1 | PWM1-T3-G1，50% 占空比 |
| CH3 | PB2 | PWM2-T3-G1，75% 占空比 |
| GND | GND | 共地 |

**main.c 开关**（L52）：
```c
#define TEST_TIMER_PWM_EN  1
```

> ⚠️ **互斥警告**（main.c L52）：PB1/PB2 与 UART2 共用，**`TEST_TIMER_PWM_EN` 不能与 `TEST_UART_EN` / `TEST_UART_RECV_EN` 等任何 UART 测试同开**。

**串口输出预期**（test_timer_pwm.c L80-93 + L96-97）：
```
===== BT892X Timer3 PWM Test =====

Timer3 PWM Configuration:
  Clock source : tmr_inc = 1MHz
  TMR3PR       = 999 (period = 1000 tick = 1ms, f = 1KHz)
  TMR3DUTY0    = 749 -> PWM0(PB0) 25% duty (high=250us, low=750us)
  TMR3DUTY1    = 499 -> PWM1(PB1) 50% duty (high=500us, low=500us)
  TMR3DUTY2    = 249 -> PWM2(PB2) 75% duty (high=250us, low=750us)

Hardware connections (logic analyzer):
  CH1 -> PB0 (PWM0, 25%)
  CH2 -> PB1 (PWM1, 50%)
  CH3 -> PB2 (PWM2, 75%)
  GND -> GND

===== PWM running, observe with logic analyzer =====
PWM alive: TMR3CNT=xxxx
PWM alive: TMR3CNT=yyyy
...
```

**逻辑分析仪波形预期**（docs/test_timer.md §3.3 实测）：
- **PB1（50%）**：周期 1ms，高电平 500µs、低电平 500µs。
- **PB2（75%）**：周期 1ms，高电平 750µs、低电平 250µs。
- **PB0（25%）**：本开发板 PB0 未引出，LA 上**无波形**（docs/test_timer.md §3.5）。可改映射到 PWM0-T3 的其他可分配脚（PB3/PE0/PE4/PF0，pinfunction §5.5 第 244 行）来验证软件逻辑无误。

**判断标准**：
- 三通道周期一致（占空比不影响周期）。
- 高电平长度严格 = `(PR − DUTY) × T_tick`。
- **频率预期修正（来自 §8 审查）**：当前代码 `INCSEL=00`，按手册 §4.2 实际是系统时钟 24MHz，故**实际 PWM 频率很可能是 ≈24kHz**（PR=999，周期 1000 × (1/24MHz) ≈ 41.67µs，f ≈ 24kHz）而非代码注释声称的 1kHz。但占空比比例仍为 25%/50%/75%。**建议上板用 LA 实际量测频率以核实**。

### 7.3 关键 bug 修复（TPND 残留导致伪中断）— docs/test_timer.md §2.3

**现象**：修复前 Part 2 实测 `999021µs, err=-979µs`（少约 1ms）。

**根因**：Part 1 末尾 TMR1 溢出但未清 TPND。进入 Part 2 后：
1. `TMR1CON = BIT(7)`（置 TIE）—— **不清 TPND**（手册 §4.2 第 258 行 TPND 只读 + CPND 写 1 清）。
2. `PICEN |= BIT(IRQ_TMR1_VECTOR)` —— TMR1 溢出挂起还在，PICPND 对应位立刻为 1。
3. 第一次中断进入比预期早 1ms → `g_t1_isr_count` 在 t0 时刻即变 1 → 1000 次提前完成。

**修复**：使能中断前加 `TMR1CPND = BIT(9);`（test_timer.c L106）。修复后 err=+21µs，count=1000。

> **教学要点**：手册 §4.2 第 264-268 行明确"TPCLR 写 1 清溢出挂起"，这是**写 CPND 而不是 CON**。任何需要在 ISR/测量前确保干净的路径，第一件事都应是 CPND=BIT(9)。

---

## 8. 审查小节（对照手册与引脚定义）

### 8.1 一致性核对（与手册一致的部分）

| 检查项 | 代码位置 | 手册/文档依据 | 结论 |
|---|---|---|---|
| TMRxCON 位定义 bit9 TPND / bit7 TIE / bit6 INCSRC / bit3:2 INCSEL / bit0 TMREN | interrupt.c L47-50、test_timer.c L46/L109/L112 | 手册 §4.2 第 256-262 行 | ✅ 一致 |
| TMRxCPND bit9 TPCLR 写 1 清挂起 | interrupt.c L32、test_timer.c L23/L35/L106 | 手册 §4.2 第 264-268 行 | ✅ 一致；L23/L32/L106 三处都有清挂起动作 |
| TMRx 周期 = PR + 1 | interrupt.c L49、test_timer.c L40/L111、test_timer_pwm.c L50 | 手册 §4.2 第 280 行 + §4.3 第 318 行 | ✅ 一致（全部用 `PR = N - 1` 公式） |
| TMR3 PWM 占空比公式：高=PR−DUTY、低=DUTY+1 | test_timer_pwm.c L62-64 | 手册 §4.3 第 330/336/342 行 | ✅ 一致（计算结果 749/499/249 对应 25/50/75%） |
| TMR3CON bit9/10/11 PWM0EN/PWM1EN/PWM2EN | test_timer_pwm.c L72-74 | 手册 §4.3 第 290-292 行 | ✅ 一致 |
| TMR3CON bit0 TMREN | test_timer_pwm.c L75 | 手册 §4.3 第 299 行 | ✅ 一致 |
| TMR3CON bit16 TPND 挂起；bit17 CPND | （未在本测试使用 CPND/溢出 ISR） | 手册 §4.3 第 286/288 行 | ✅ 一致（手册 §4.3 区分 TPND=bit16 与 TMR0/1/2 的 TPND=bit9，是重要差异，见 §8.3.3） |
| PICPR[x]=0 = 高优先级 | test_timer.c L113 | 手册 §2.3 第 106-109 行 | ✅ 一致 |
| PICEN[x]=1 使能向量 | test_timer.c L114、interrupt.c L52 | 手册 §2.3 第 94-97 行 | ✅ 一致 |
| GIE / LPINTEN / GIEM 全开 | main.c L245 `PICCON |= 0x10003` | 手册 §2.3 第 64-73 行 | ✅ 一致（bit0=GIE, bit1=LPINTEN, bit16=GIEM） |
| tmr_inc = 1MHz（main.c） | main.c L231-233 | 工程内统一约定 | ✅ 一致（CLKCON2[31:24]=25 分频 26 得 1MHz；CLKCON0 bit24 选 x26m_div_clk） |
| TMR2 作为 1µs delay tick | main.c L100-106 + L121-145 | 工程内约定 | ✅ 一致 |
| TMR0 1ms ISR | interrupt.c L28-53 | 工程内约定 | ✅ 一致 |
| FUNCMCON2[11:8]=0x1 → TMR3 选 G1 | test_timer_pwm.c L38 | 手册 §3.3 第 185 行 + pinfunction §4.2 | ✅ 一致（G1=PWM0-T3-G1=PB0、PWM1-T3-G1=PB1、PWM2-T3-G1=PB2） |
| FUNCMCON2[7:4]=0xF → 清 TMR3CPTMAP | test_timer_pwm.c L38 | 手册 §3.3 第 186 行 `1111=清除` | ✅ 一致 |
| FUNCMCON0[3:0] \|= 0xF 释放 SD0MAP | test_timer_pwm.c L36 | 手册 §3.3 第 170 行 | ✅ 一致（虽然这里是 \|= 而非 =，但因默认 0x0 等价于 =） |
| GPIOBFEN/DE/DIR 配置 PB0/1/2 为功能输出 | test_timer_pwm.c L41-43 | 手册 §3.2 第 148/155/156 行 | ✅ 一致 |
| IRQ_TMR1_VECTOR = 4 | test_timer.c L100 + int.h L7 | 手册 §2.2 第 53 行 | ✅ 一致 |
| IRQ_TMR0_VECTOR = 3 | interrupt.c L46 + int.h L6 | 手册 §2.2 第 52 行 | ✅ 一致 |

### 8.2 ⚠️ 发现的疑点（重点）

#### 8.2.1 【核心疑点】`test_timer_pwm.c` 的 INCSEL=00 与代码注释/手册 §4.2 矛盾

| 项目 | 内容 |
|---|---|
| **位置** | `test/test_timer_pwm.c` L74 `(0 << 2)` 和 L47/L81/L83 注释 "tmr_inc = 1MHz" |
| **手册 §4.2 第 261 行** | INCSEL=00 = **系统时钟**；INCSEL=01 = 计数器输入上升沿（即 tmr_inc） |
| **实际效果** | TMR3 的 INCSEL 写 00 → 计数器实际由 Fsys（≈24MHz）驱动，**不是 tmr_inc 的 1MHz** |
| **对照其他定时器** | `interrupt.c` L50 用 `BIT(2)` = INCSEL=01；`main.c` L105 用 `BIT(2)` = INCSEL=01；`test_timer.c` L46/L112 用 `BIT(2)` = INCSEL=01。**只有 `test_timer_pwm.c` 例外** |
| **影响** | 1. 实测频率很可能是 ≈ 24kHz 而非代码注释声称的 1kHz（PR=999，1/24MHz × 1000 ≈ 41.67µs → f ≈ 24kHz）。<br/>2. **占空比不受影响**：占空比公式只与 PR 和 DUTY 的比值有关，与计数时钟频率无关（手册 §4.3 公式 (PR−DUTY)/(PR+1)）。<br/>3. docs/test_timer.md §3.4 实测 PB1/PB2 周期 "约 1ms" 与代码注释一致，但与 INCSEL=00 矛盾——**这暗示测试报告与代码注释可能基于历史数据，或 LA 实测未精确量频率**。 |
| **建议（不要求改代码）** | 上板后用逻辑分析仪实测 PB1 周期：<br/>- 若 f ≈ 1kHz → 代码注释正确，但手册 §4.2 在本芯片上是错的（罕见）；<br/>- 若 f ≈ 24kHz → 代码注释与手册 §4.2 一致，**test_timer_pwm.c 需将 `(0<<2)` 改为 `(1<<2)`** 才能得到 1kHz。 |
| **记录原则** | 仅记录，不改代码。 |

#### 8.2.2 `test_timer_pwm.c` L36 `FUNCMCON0 |= 0xF` 用 \|= 而非 =

| 项目 | 内容 |
|---|---|
| **位置** | `test/test_timer_pwm.c` L36 `FUNCMCON0 |= 0xF;` |
| **意图** | 注释说 "SD0MAP = 0xF (clear, 释放 PB0)" |
| **问题** | `\|= 0xF` 是 read-modify-write，**只动 [3:0]**。如果之前 SD0MAP 已配成 0x1/0x2 等非零非 0xF 值，结果不会变成 0xF 而是 `原值 \| 0xF = 0xF`。当前 main.c 中 SD0MAP 默认 0（手册 §3.3 第 170 行 default 0x0），`\|= 0xF` 与 `= 0xF` 等价；一旦未来在 main.c 改了 SD0MAP，此处 `\|=` 的安全性会反向——保留历史高位。 |
| **建议** | 用 `FUNCMCON0 = (FUNCMCON0 & ~0xF) | 0xF;` 或更清晰地用 `= 0xF`；但**仅是风格建议**。 |

#### 8.2.3 TMR3 的 TPND 位位置与 TMR0/1/2 不同（手册设计差异，未在测试中使用）

| 项目 | 内容 |
|---|---|
| **手册 §4.2 vs §4.3** | TMR0/1/2 的 TPND 在 `TMRxCON[bit9]`（§4.2 第 258 行），TMR3/4/5 的 TPND 在 `TMRxCON[bit16]`（§4.3 第 289 行）。清挂起寄存器分别为 `TMRxCPND[9]`（§4.2 第 268 行）与 `TMRxCPND[16]`（§4.3 第 306 行）。 |
| **影响** | 若未来要给 TMR3 加 ISR 测试，需注意 `TMR3CPND = BIT(16)`，**不是** `BIT(9)`。 |
| **本测试** | test_timer_pwm.c 不使用 TMR3 ISR，仅用 PWM 输出，无影响。 |

#### 8.2.4 PB0 (PWM0-T3-G1) 在本开发板上无输出（已知硬件问题，非代码 bug）

| 项目 | 内容 |
|---|---|
| **位置** | docs/test_timer.md §3.5 |
| **现象** | PB0/PWM0 在 LA 上无波形，PB1/PWM1、PB2/PWM2 正常。 |
| **判断** | 寄存器配置对三路一致（同时 PWM0EN/PWM1EN/PWM2EN），PB1/PB2 正常说明 TMR3 + G1 映射与 PWM 逻辑无误。**PB0 单独异常，疑似本开发板未把 PB0 引脚引出**。 |
| **建议** | 改映射到 PWM0-T3 的其他可分配脚（PB3/PE0/PE4/PF0，pinfunction §5.5 第 244 行），避开已占用脚即可。 |

#### 8.2.5 `interrupt.c` L36-37 `tick_cnt % 5` 分支无任何代码

| 项目 | 内容 |
|---|---|
| **位置** | `interrupt.c` L35-37 `if ((tick_cnt % 5) == 0) { //5ms }` |
| **判断** | 占位空分支，无 bug；推测为预留 5ms 钩子。 |

### 8.3 总结

| 类别 | 一致 | 疑点 |
|---|---|---|
| 位定义（手册 §4.2/§4.3） | ✅ 全一致 | — |
| 周期/占空比公式 | ✅ 全一致 | — |
| 引脚映射（手册 §3.3 + pinfunction） | ✅ 全一致 | — |
| 中断控制（手册 §2.3） | ✅ 全一致 | — |
| PWM 频率注释 vs 实际 INCSEL 配置 | ⚠️ 矛盾 | **§8.2.1 INCSEL=00 vs 注释"tmr_inc=1MHz"，可能致实测 f ≈ 24kHz 而非 1kHz** |
| 代码风格 | ✅ 大体一致 | §8.2.2 `\|= 0xF` 风格 + §8.2.5 空分支 |
| 硬件可达性 | — | §8.2.4 PB0 未引出（已知，非代码） |

**审查结论**：除 §8.2.1 的 INCSEL 矛盾需上板用 LA 实测频率复核外，其余代码与手册 §4/§3.3/§2.3 一致，可继续维护。**不要因审查结论修改代码**——疑点仅作记录。

---

> **文档结束**。后续维护者请重点关注 §8.2.1 的 PWM 频率疑点，并按 §7.2 的接线与预期复核 TMR3 PWM 的实测频率。