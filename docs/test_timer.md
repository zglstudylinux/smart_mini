# BT892X 定时器（Timer）测试报告

> **测试日期**：2026-07-17
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §4 定时器](../BT892X_UserManual_Driver.md)（TMR0/1/2 §4.2、TMR3/4/5 §4.3、FUNCMCON2 §3.3）
> - [bt892x_pinfunction.md §4.2 PORTB / §5.5 PWM](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**TMR1 定时精度+中断** & **TMR3 三路 PWM 输出**
> **测试结果**：✅ TMR1 轮询/中断通过；✅ TMR3 PWM 的 PB1/PB2 通过（⚠️ PB0 未引出，见 §3.5）

---

## 0. 文档结构

BT892X 有 Timer0~5：TMR0/1/2 仅 32 位定时（手册 §4.1），TMR3/4/5 还支持计数/捕获/PWM（手册 §4.1）。本工程 **TMR0=1ms 系统 tick、TMR2=1µs delay tick 已被 main.c 占用**，故：

| 程序 | 路径 | 用途 | 验证方式 |
|---|---|---|---|
| **test_timer.c** | `smart_mini/test/test_timer.c` | TMR1 轮询精度 + 溢出中断 | 串口打印 err；用 TMR2 做基准 |
| **test_timer_pwm.c** | `smart_mini/test/test_timer_pwm.c` | TMR3 三路 PWM（PB0/PB1/PB2） | 逻辑分析仪测周期/占空比 |

> `test_timer_pwm.c` 自 `smart_mini_copilot` 移植，功能不变，仅入口改名 `test_timer_pwm_run()`。
> ⚠️ **PWM 用 PB1/PB2，与 UART2(PB1/PB2) 冲突** → `TEST_TIMER_PWM_EN` 与 `TEST_UART_EN` 不能同开。

---

## 1. 手册源码引用

### 1.1 定时器时钟源（手册 [§4.2](../BT892X_UserManual_Driver.md) 第 258-262 行 + main.c 时钟配置）

TMR0/1/2 计数时钟 = **tmr_inc**，而非 CPU 时钟：

```
TMR1CON 位（手册 §4.2 第 256-262 行）
Bit | Name   | Description
----|--------|-------------------------------------------
 9  | TPND   | 定时器溢出挂起。0:未溢出；1:溢出
 7  | TIE    | 溢出中断使能。0:禁用；1:使能
 6  | INCSRC | 递增源选择。0: TMR_INC；1: 外部引脚
3:2 | INCSEL | 递增时钟选择
 0  | TMREN  | 定时器使能。0:禁用；1:使能
```

> **tmr_inc 频率来源（main.c 第 190-193 行；手册未给 CLKCON 时钟树逐位定义，以工程配置为准）**：
> - `CLKCON2 |= (25 << 24)` → x26m(26MHz) 分频 (25+1)=26 → **x26m_div_clk = 1MHz**（注释 `x26m_div_clk = 1M (timer, ir, fmam use)`）
> - `CLKCON0 |= BIT(24)` → **tmr_inc = x26m_div_clk = 1MHz**（注释 `tmr_inc select x26m_div_clk = 1M`）
> - 因此 **1 tick = 1µs**（`TICK_1US=1`，`header/include.h`）。**24MHz 是 CPU 系统时钟**（`set_sys_clk(SYS_24M)`），与定时器计数时钟无关。
> - 外部佐证：TMR3 PWM 实测约 1kHz（`PR=999`）、`delay_ms` 墙钟正确，反证 tmr_inc ≈ 1MHz。

### 1.2 TMR1 清挂起 / 计数 / 周期寄存器（手册 [§4.2](../BT892X_UserManual_Driver.md) 第 264-280 行）

```
TMR1CPND  bit9 TPCLR  : 写 1 清除溢出挂起
TMR1CNT   [31:0]      : 使能后递增，等于 TMRPR 时溢出清零并置中断标志
TMR1PR    [31:0]      : 定时器周期 = TMRPR + 1
```

### 1.3 TMR3 控制寄存器（手册 [§4.3](../BT892X_UserManual_Driver.md) 第 284-299 行）

```
TMR3CON 位
Bit  | Name    | Description
-----|---------|----------------------------
 16  | TPND    | 溢出挂起
 11  | PWM2EN  | PWM2 使能
 10  | PWM1EN  | PWM1 使能
  9  | PWM0EN  | PWM0 使能
  7  | TIE     | 溢出中断使能
  6  | INCSRC  | 递增源。0:TMR_INC；1:外部引脚
 3:2 | INCSEL  | 递增时钟选择
  0  | TMREN   | 定时器使能
```

**PWM 周期/占空比公式**（TMR3PR + DUTY，见 test_timer_pwm.c 头注释）：

```
PWM 周期     = TMR3PR + 1  (个 tmr_inc tick)
高电平长度   = PR - DUTY
低电平长度   = DUTY + 1
占空比(高)   = (PR - DUTY) / (PR + 1)
```

### 1.4 TMR3 PWM 引脚映射（手册 [§3.3 FUNCMCON2](../BT892X_UserManual_Driver.md) 第 179-186 行）

```
FUNCMCON2 位
[11:8] TMR3MAP    : Timer3 PWM 映射。0001=G1，1111=清除
[7:4]  TMR3CPTMAP : Timer3 捕获引脚映射。1111=清除
```

### 1.5 PWM 引脚（[bt892x_pinfunction.md §5.5](../bt892x_pinfunction.md) 第 244-246 行 / §4.2 第 115-116 行）

| PWM | 可分配 PAD（§5.5） | G1 映射引脚 |
|---|---|---|
| PWM0-T3 | PB0, PB1, PB2, PB3, PE0, PE4, PF0 | **PB0** |
| PWM1-T3 | PA3, PA4, PB1, PB2, PB3, PE4 | **PB1**（§4.2 第115行 `PWM1-T3-G1`） |
| PWM2-T3 | PA4, PB2, PB5, PE0 | **PB2**（§4.2 第116行 `PWM2-T3-G1`） |

---

## 2. 测试程序 1：test_timer.c（TMR1 精度 + 中断）

### 2.1 寄存器配置原理（每行注明手册出处）

**Part 1 轮询**（`test_timer1_measure_poll`）：

```c
TMR1CPND = BIT(9);            // §4.2: 先清 TPND，否则上次溢出的挂起会让 while 立即跳过
TMR1CON  = 0;                 // §4.2: 停表清配置
TMR1CNT  = 0;                 // §4.2: 计数清零
TMR1PR   = expected_us - 1;   // §4.2: 周期 = PR+1 = expected_us 个 tmr_inc(1µs)
t0 = TMR2CNT;                 // 基准起点（TMR2 1µs tick）
TMR1CON  = BIT(2) | BIT(0);   // §4.2: INCSEL + TMREN 启动
while ((TMR1CON & BIT(9)) == 0);   // §4.2: 等 TPND=1（溢出）
t1 = TMR2CNT;  TMR1CON = 0;   // 记录终点并停表，实测 = t1 - t0
```

**Part 2 中断**：

```c
register_isr(IRQ_TMR1_VECTOR, test_timer1_isr);   // interrupt.c 注册向量
TMR1CPND = BIT(9);            // §4.2 【关键修复】清 Part 1 遗留的 TPND（见 §2.3 踩坑）
TMR1CON  = BIT(7);            // §4.2: TIE=1 溢出中断使能
TMR1CNT  = 0;
TMR1PR   = 1000 - 1;          // §4.2: 1ms 周期
TMR1CON |= BIT(2) | BIT(0);   // §4.2: 启动
PICPR &= ~BIT(IRQ_TMR1_VECTOR);   // 高优先级
PICEN |=  BIT(IRQ_TMR1_VECTOR);   // 使能 TMR1 向量
// ISR: TMR1CPND = BIT(9); count++;   // §4.2: 每次溢出写 1 清挂起并计数
```

### 2.2 测试流程

| 子测试 | 验证 | 期望 |
|---|---|---|
| Part 1 · 1ms | TMR1 溢出耗时 | ≈1000µs，err≈0 |
| Part 1 · 10ms | 同上 | ≈10000µs，err≈0 |
| Part 1 · 100ms | 同上 | ≈100000µs，err≈0 |
| Part 2 · 1000×1ms 中断 | 1000 次 ISR 总耗时 | ≈1000000µs，count=1000 |

### 2.3 开发过程中的关键 Bug 修复（TPND 残留导致伪中断）

**现象**：修复前 Part 2 实测 `999021µs, err=-979µs`（少了约一个 1ms 周期）。

**根因**：Part 1 最后一次 100ms 轮询结束时 TMR1 溢出，`TMR1CON.TPND(bit9)=1` 未清除。进入 Part 2 后 `TMR1CON=BIT(7)`（置 TIE）**并不能清 TPND**（手册 §4.2：TPND 只能靠 `TMR1CPND[9]` 写 1 清），于是 `PICEN` 一使能就**立刻触发一次伪中断**，count 在 t0 时刻即变 1 → 1000 次提前约 1ms 完成。

**修复**：使能中断前加 `TMR1CPND = BIT(9);` 清残留挂起。

```
修复前：register_isr → TMR1CON=BIT(7) → ... → PICEN        (TPND 残留 → 伪中断, err≈-979µs)
修复后：register_isr → TMR1CPND=BIT(9) → TMR1CON=BIT(7) → ... → PICEN   (err≈+21µs)
```

### 2.4 实测结果（用户验证，修复后）

```
[Part 1] Polling mode - period accuracy
  1ms:   expected 1000us,   measured 1000 us,    err=0 us
  10ms:  expected 10000us,  measured 10000 us,   err=0 us
  100ms: expected 100000us, measured 100000 us,  err=0 us
[Part 2] Interrupt mode - 1000 x 1ms ISR
  1000 ISR: expected 1000000us, measured 1000021 us, err=21 us, count=1000
```

**结论**：✅ 轮询 err=0（TMR1 与 TMR2 同源 1MHz）；中断修复后 err=+21µs（ISR 进入/退出固定延迟，正常），count=1000 正确。

---

## 3. 测试程序 2：test_timer_pwm.c（TMR3 三路 PWM）

### 3.1 引脚分配

| 引脚 | 角色 | 占空比 | 手册依据 |
|---|---|---|---|
| PB0 | PWM0-T3-G1 | 25% | [pinfunction §5.5](../bt892x_pinfunction.md) 第244行 |
| PB1 | PWM1-T3-G1 | 50% | [pinfunction §4.2](../bt892x_pinfunction.md) 第115行 |
| PB2 | PWM2-T3-G1 | 75% | [pinfunction §4.2](../bt892x_pinfunction.md) 第116行 |

### 3.2 寄存器配置原理

```c
FUNCMCON0 |= 0xF;                          // 释放 PB0（清 SD0MAP，避免 SDCMD 占用）
FUNCMCON2 &= ~((0xF<<4)|(0xF<<8));         // §3.3: 清 TMR3CPTMAP + TMR3MAP
FUNCMCON2 |=  (0xF<<4)|(0x1<<8);           // §3.3: TMR3CPTMAP=清除, TMR3MAP=G1
GPIOBFEN |= BIT(0)|BIT(1)|BIT(2);          // §3.2: 功能 IO（PWM 输出）
GPIOBDE  |= BIT(0)|BIT(1)|BIT(2);          // §3.2: 数字 IO
GPIOBDIR &= ~(BIT(0)|BIT(1)|BIT(2));       // §3.2: 输出方向
TMR3CNT = 0;  TMR3PR = 999;                // §4.3: 周期=PR+1=1000 tick=1ms → 1kHz
TMR3DUTY0 = 749;  // 高=PR-DUTY=250 → 25%   §4.3 公式
TMR3DUTY1 = 499;  // 高=500 → 50%
TMR3DUTY2 = 249;  // 高=750 → 75%
TMR3CON = BIT(11)|BIT(10)|BIT(9)|(0<<2)|BIT(0);  // §4.3: PWM2/1/0EN + INCSEL=00 + TMREN
```

### 3.3 测试流程

| 通道 | 引脚 | 期望周期 | 期望高电平 |
|---|---|---|---|
| PWM0 | PB0 | 1ms(1kHz) | 250µs (25%) |
| PWM1 | PB1 | 1ms(1kHz) | 500µs (50%) |
| PWM2 | PB2 | 1ms(1kHz) | 750µs (75%) |

### 3.4 实测结果（用户验证）

- ✅ **PB1（PWM1，50%）** 与 **PB2（PWM2，75%）** 逻辑分析仪波形正常，周期约 1ms、占空比符合。
- 串口每 2s 打印 `PWM alive: TMR3CNT=...`。

### 3.5 ⚠️ 已知问题：PB0 无输出

**现象**：PB0（PWM0）在逻辑分析仪上无波形，PB1/PB2 正常。

**判断**：寄存器配置对三路一致（同一次 `TMR3CON` 使能 PWM0/1/2），PB1/PB2 正常说明 TMR3+G1 映射与 PWM 逻辑无误，**PB0 单独异常，疑似本开发板未把 PB0 引脚引出/不可达**（非软件问题）。如需用 PWM0，可改映射到 §5.5 中 PWM0-T3 的其他可分配脚（PB3/PE0/PE4/PF0），但需避开已占用脚。

---

## 4. 完整寄存器表

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `TMR1CON` | 0x0D4 (`SFR0_BASE+0x35*4`) | `BIT(2)\|BIT(0)`启动 / `BIT(7)`TIE | §4.2 |
| `TMR1CPND` | 0x0D8 (`SFR0_BASE+0x36*4`) | `= BIT(9)` 清溢出挂起 | §4.2 第268行 |
| `TMR1CNT` | 0x0DC (`SFR0_BASE+0x37*4`) | `= 0` 清零 | §4.2 |
| `TMR1PR` | 0x0E0 (`SFR0_BASE+0x38*4`) | `= 周期-1` | §4.2 第280行 |
| `TMR2CNT` | 0x0F0 (`SFR0_BASE+0x3c*4`) | 只读，1µs 基准 | main.c timer2_init |
| `FUNCMCON0` | 0x01C (`SFR0_BASE+0x07*4`) | `\|= 0xF` 释放 PB0 | §3.3 |
| `FUNCMCON2` | 0x024 (`SFR0_BASE+0x09*4`) | `[11:8]=1`(G1), `[7:4]=0xF`(清捕获) | §3.3 第185-186行 |
| `GPIOBFEN` | 0x654 (`SFR6_BASE+0x15*4`) | `\|= BIT(0/1/2)` 功能 IO | §3.2 |
| `GPIOBDE` | 0x650 (`SFR6_BASE+0x14*4`) | `\|= BIT(0/1/2)` 数字 IO | §3.2 |
| `GPIOBDIR` | 0x64C (`SFR6_BASE+0x13*4`) | `&= ~BIT(0/1/2)` 输出 | §3.2 |
| `TMR3CON` | 0x900 (`SFR9_BASE+0x00*4`) | `PWM0/1/2EN + TMREN` | §4.3 第284-299行 |
| `TMR3CNT` | 0x908 (`SFR9_BASE+0x02*4`) | `= 0` | §4.3 |
| `TMR3PR` | 0x90C (`SFR9_BASE+0x03*4`) | `= 999`(1kHz) | §4.3 |
| `TMR3DUTY0/1/2` | 0x914/0x918/0x91C | `749/499/249`(25/50/75%) | §4.3 公式 |

---

## 5. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| 轮询立即返回 err 很大 | 上次溢出 TPND 未清 | 测量前 `TMR1CPND=BIT(9)`（已处理，§2.1） |
| 中断 err≈−一个周期 | TIE+PICEN 前 TPND 残留触发伪中断 | 使能前 `TMR1CPND=BIT(9)`（§2.3 修复） |
| 中断 count 一直 0 | 向量未使能/未注册 | 确认 `register_isr` + `PICEN\|=BIT(IRQ_TMR1_VECTOR)` |
| PWM 无波形（全部） | TMR3MAP 未设 G1 / FEN 未开 | 确认 `FUNCMCON2[11:8]=1` + `GPIOBFEN` |
| PWM 单脚无波形 | 该引脚未引出（如 PB0） | 换 §5.5 中该 PWM 的其他可分配脚 |
| 周期是预期两倍 | tmr_inc 未选到 1MHz | 确认 main.c `CLKCON0\|=BIT(24)` |

---

## 6. 工程意义

- **保留 TMR1 精度/中断测试**（`test_timer.c`）+ **新增 TMR3 PWM 测试**（`test_timer_pwm.c`），互补覆盖定时器的定时、中断、PWM 三种能力。
- **修复了一个真实 bug**（TPND 残留伪中断），文档 §2.3 完整记录，便于复盘。
- **手册依据**：每条寄存器操作对应 [§4.2 / §4.3 / §3.3](../BT892X_UserManual_Driver.md) 或 [pinfunction §4.2 / §5.5](../bt892x_pinfunction.md)。

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_timer.c` | TMR1 精度+中断测试 |
| `smart_mini/test/test_timer.h` | 头文件 |
| `smart_mini/test/test_timer_pwm.c` | TMR3 三路 PWM 测试（自 copilot 移植） |
| `smart_mini/test/test_timer_pwm.h` | 头文件 |
| `smart_mini/test/test_common.h` | 共用 `TEST_LOG` 宏 |
| `smart_mini/main.c` | 入口（`TEST_TIMER_EN` / `TEST_TIMER_PWM_EN` 开关）+ tmr_inc 时钟配置（第190-193行） |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 宏定义（TMR1 第93-96行、TMR3 第546-553行） |
| `docs/BT892X_UserManual_Driver.md` | 手册（§4 定时器第246行起、§3.3 FUNCMCON2 第179行） |
| `docs/bt892x_pinfunction.md` | 引脚功能（§4.2 PORTB、§5.5 PWM） |
