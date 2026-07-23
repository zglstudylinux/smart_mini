# BT892X ADKEY 测试报告

> **测试日期**：2026-07-20
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考资料**：
> - [SARADC_CTL（逐次逼近型 ADC）数据手册摘要](SARADC_CTL%20%28逐次逼近型%20ADC%29%20数据手册摘要.md)
> - [bt892x_pinfunction.md §4.2 PORTB / §8.10 SARADC](bt892x_pinfunction.md)
> - [BT892X_UserManual_Driver.md §3 GPIO](BT892X_UserManual_Driver.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **当前阶段**：阶段一原始采样 + 阶段二三键映射 + 阶段三 5ms×5 消抖 + 阶段四 700ms 长按/长抬 + 阶段五 200ms HOLD 连发
> **测试结果**：✅ PB5/ADC12 原始采样通过；✅ 三键映射通过；✅ 25ms 消抖及 SHORT/SHORT_UP 通过；✅ 700ms LONG/LONG_UP 通过；✅ 200ms HOLD 连发通过

---

## 0. 文档结构

| 程序 | 路径 | 入口/开关 | 用途 |
|---|---|---|---|
| **test_adkey.c** | `smart_mini/test/test_adkey.c` | `test_adkey_raw_run` / `TEST_ADKEY_RAW_EN` | 初始化 PB5/ADC12，每 100ms 采样并打印原始值 |
| | | `test_adkey_map_run` / `TEST_ADKEY_MAP_EN` | 使用实测阈值映射 NONE/PLAY/PREV/NEXT |
| | | `test_adkey_debounce_run` / `TEST_ADKEY_DEBOUNCE_EN` | TMR1 每 5ms 扫描，连续 5 次相同后输出短按/短抬消息 |
| | | `test_adkey_long_run` / `TEST_ADKEY_LONG_EN` | 阶段三状态机 + 700ms 长按/长抬消息（已并入阶段五，本版本被替换为引导日志+死循环） |
| | | `test_adkey_hold_run` / `TEST_ADKEY_HOLD_EN` | 阶段四 700ms + LONG 之后每 200ms HOLD + LONG_UP |
| **test_adkey.h** | `smart_mini/test/test_adkey.h` | — | 测试入口声明 |

阶段一验证原始 ADC 采集；阶段二完成上拉组合对照和原始值到键值的映射；阶段三加入 TMR1 5ms 扫描及连续 5 次相同值的按下/抬起消抖；阶段四加入 700ms 长按与长抬；阶段五加入 LONG 之后每 200ms HOLD 连发。本轮是阶段五的实现、测试和验收。

---

## 1. 硬件依据

### 1.1 PB5 对应 ADC12

引脚定义表中 PB5 标记为 `PB5/WKO`，ADC 功能为 ADC12；SARADC 通道分配表也明确列出 `ADC12 → PB5`。

PB5 同时是 10S Reset 主唤醒源。本测试经用户确认允许将 PB5 用作板载 ADKEY，但只将它配置为输入，不配置为输出，也不修改唤醒和复位控制寄存器。

### 1.2 板载按键电路

PWRKEY 网络最终连接 PB5/ADC12，无需额外接线：

| 按键 | 键名 | PWRKEY 到 GND 的电阻 |
|---|---|---:|
| S5 P/P | PLAY | 0Ω（直接接地） |
| S6 PREV | PREV | 12kΩ |
| S7 NEXT | NEXT | 47kΩ |

PB5 使用 GPIO 10kΩ上拉，同时使能 ADC12 内部 100kΩ上拉。三个按键按下后形成不同分压，由 ADC12 转换为原始数字值。

---

## 2. SARADC 配置依据

### 2.1 时钟树

用户提供的 BT892X 时钟树明确给出：

| 配置 | 含义 |
|---|---|
| `CLKCON0[28]=0` | SARADC 时钟源为 `rc2m_clk` |
| `CLKCON0[28]=1` | SARADC 时钟源为 `x24m_clkdiv4 = 6MHz` |
| `CLKGAT0[13]=1` | 打开 SARADC 时钟门 |

本测试选择稳定的 6MHz 晶振分频时钟。

### 2.2 SARADC 位时钟

数据手册公式：

```text
SARADC_CLK = Fadc_clock / [2 × (SADCBAUD + 1)]
```

配置 `SADCBAUD=5`：

```text
SARADC_CLK = 6MHz / [2 × (5 + 1)] = 500kHz
```

500kHz 低于数据手册规定的 1MHz 最大位时钟，且远高于按键扫描所需速度。

### 2.3 控制位

| 寄存器位 | 配置 | 手册含义 |
|---|---:|---|
| `SADCCON[19] ADCAEN` | 1 | 自动使能 SARADC 模拟模块 |
| `SADCCON[18] ADCANGIO` | 1 | 自动使能模拟 IO |
| `SADCCON[17] ADCIE` | 0 | 阶段一使用轮询，不启用 ADC 中断 |
| `SADCCON[16] ADCEN` | 1 | 使能 SARADC |
| `SADCCON[12] CH12PUEN` | 1 | 使能 ADC12 内部 100kΩ上拉 |
| `SADCCH[12] CH12EN` | 每次采样写 1 | 清除 ADCPND 并启动 ADC12 转换 |
| `SADCCH[16] ADCPND` | 轮询至 1 | 转换完成 |
| `SADCDAT12[9:0]` | 读取 | ADC12 的 10 位结果 |

通道建立时间在阶段一保持手册默认值 `SADCST.CH12ST=0`。每次读取后，下一次采样都会重新写 `SADCCH=BIT(12)` 启动新转换。

### 2.4 PB5 GPIO 配置

依据 GPIO 手册 §3.2：

```c
GPIOBDIR    |=  BIT(5);   // 输入
GPIOBDE     |=  BIT(5);   // 数字输入使能
GPIOBFEN    &= ~BIT(5);   // 不映射到其它外设
GPIOBPU     |=  BIT(5);   // 10kΩ上拉
GPIOBPD     &= ~BIT(5);   // 关闭 10kΩ下拉
GPIOBPU200K &= ~BIT(5);   // 关闭其它上拉
GPIOBPD200K &= ~BIT(5);
GPIOBPU300  &= ~BIT(5);
GPIOBPD300  &= ~BIT(5);
```

---

## 3. 测试程序

### 3.1 初始化顺序

```text
1. CLKCON0[28]=1，选择 x24m_clkdiv4
2. CLKGAT0[13]=1，打开 SARADC 时钟
3. PB5 配置为输入并选择 10kΩ上拉
4. SADCBAUD=5，得到 500kHz ADC 位时钟
5. SADCST=0，保持默认建立时间
6. SADCCON 设置 ADCAEN/ADCANGIO/ADCEN/CH12PUEN
```

### 3.2 单次采样顺序

```text
1. SADCCH = BIT(12)，启动 ADC12 转换并清 ADCPND
2. 轮询 SADCCH[16]，最多等待 5000µs
3. 读取 SADCDAT12 & 0x03FF
4. 等待 100ms 后重新使能通道并进行下一次转换
```

如果 5ms 内未完成，会打印 `[TIMEOUT]`，避免测试程序静默卡死。

### 3.3 打印方式

每个样本打印原始值，每 10 个样本汇总一个固定的 1 秒窗口：

```text
[TEST] ADC12 raw=122 sample=10
[TEST] 1s window: min=122 max=122 span=0
```

按键动作发生在某个窗口中间时，该窗口会同时包含抬起值和按下值。例如从 NONE=122 切到 PLAY=24 的首个窗口为 `min=24 max=122`。这属于正常的状态切换窗口；持续按住后的后续窗口才代表该按键的稳定范围。

---

## 4. 用户实测结果

### 4.1 测试流程

| 顺序 | 操作 | 期望 |
|---:|---|---|
| 1 | 所有按键抬起 | 得到稳定 NONE 原始值 |
| 2 | 持续按住 P/P | 得到稳定 PLAY 原始值 |
| 3 | 松开 P/P | 回到 NONE |
| 4 | 持续按住 PREV | 得到稳定 PREV 原始值 |
| 5 | 松开 PREV | 回到 NONE |
| 6 | 持续按住 NEXT | 得到稳定 NEXT 原始值 |
| 7 | 松开 NEXT | 回到 NONE |

### 4.2 启动输出

```text
Hello SMART Flash MiniProj
[TEST] ========================================
[TEST] ADKEY stage 1: PB5 / ADC12 raw sampling
[TEST] SARADC clock: x24m/4=6MHz, baud=5, ADC clock=500kHz
[TEST] PB5 pull-up: GPIO 10K + ADC12 100K
[TEST] SARADC analog auto-enable: ADCAEN=1, ADCANGIO=1
[TEST] Press in order: NONE -> PLAY -> PREV -> NEXT
[TEST] ========================================
```

### 4.3 四档稳定值

| 状态 | 稳定窗口 | 稳定中心值 | 与上一档间隔 |
|---|---|---:|---:|
| PLAY（P/P） | `min=24 max=24 span=0` | 24 | — |
| PREV | `min=115 max=115 span=0` | 115 | 91 LSB |
| NEXT | `min=120 max=120 span=0` | 120 | 5 LSB |
| NONE | `min=122 max=122 span=0` | 122 | 2 LSB |

实测原始输出节选：

```text
# NONE
[TEST] 1s window: min=122 max=122 span=0

# 按下 P/P 的切换窗口
[TEST] 1s window: min=24 max=122 span=98
# 持续按住 P/P
[TEST] 1s window: min=24 max=24 span=0
[TEST] 1s window: min=24 max=24 span=0

# 持续按住 PREV
[TEST] 1s window: min=115 max=115 span=0
[TEST] 1s window: min=115 max=115 span=0
[TEST] 1s window: min=115 max=115 span=0

# 持续按住 NEXT
[TEST] 1s window: min=120 max=120 span=0
[TEST] 1s window: min=120 max=120 span=0
[TEST] 1s window: min=120 max=120 span=0

# 松开后恢复 NONE
[TEST] 1s window: min=122 max=122 span=0
```

全程未出现 `[TIMEOUT]`。

### 4.4 结论

✅ **阶段一通过**：PB5/ADC12 可以稳定采集板载三个 ADKEY 按键；PLAY、PREV、NEXT 和 NONE 得到四个互不相同且持续按住时 `span=0` 的原始值，转换触发、完成轮询和数据读取均正常。

⚠️ **后续风险**：NEXT=120 与 NONE=122 目前仅相差 2 LSB。该结果足以证明阶段一原始采集和四档可区分，但不宜直接凭单次测试写死最终产品阈值。阶段二应先进行更长时间的重复采样，并在必要时对“GPIO 10kΩ + ADC 100kΩ、仅 GPIO 10kΩ、仅 ADC 100kΩ”进行对照，确保 NEXT/NONE 在温度、电源和按键重复操作下不会重叠。

---

## 5. 开发过程中的配置修正

### 5.1 现象

初版只设置 `ADCEN + CH12PUEN`。ADC 能完成转换并对按键产生响应，但缺少自动模拟模块和模拟 IO 使能的明确配置。

### 5.2 修正

依据数据手册 `SADCCON` 位表，增加：

```c
SADCCON = BIT(19)    // ADCAEN
          | BIT(18)  // ADCANGIO
          | BIT(16)  // ADCEN
          | BIT(12); // CH12PUEN
```

修正版启动输出会明确打印：

```text
[TEST] SARADC analog auto-enable: ADCAEN=1, ADCANGIO=1
```

用户使用修正版完成了上述稳定值测试。

---

## 6. 失败排查

| 现象 | 检查项 |
|---|---|
| 持续打印 `[TIMEOUT]` | 检查 `CLKCON0[28]`、`CLKGAT0[13]`、`ADCEN` 和每次采样前的 `SADCCH=BIT(12)` |
| 所有状态固定为一个值 | 检查 PWRKEY 是否连接 PB5/ADC12，以及 PB5 是否错误映射到其它外设 |
| 按下后首个窗口同时出现两个值 | 正常切换窗口；继续按住，观察后续完整窗口 |
| PLAY/PREV/NEXT 无法分开 | 检查 S5/S6/S7、12kΩ/47kΩ及两路内部上拉配置 |
| NEXT 与 NONE 偶发混淆 | 延长重复采样，比较不同上拉组合后再确定阈值 |
| 长按 P/P 后芯片复位 | PB5 兼作 10S Reset；原始值测试不要按住超过 10 秒 |

---

## 7. 阶段二：上拉组合裕量对照

阶段一双上拉结果中 NEXT=120、NONE=122，仅相差 2 LSB。为避免直接写入缺少依据的阈值，分别实测三种手册允许的上拉组合。

### 7.1 对照结果

| 上拉组合 | PLAY | PREV | NEXT | NONE | 结论 |
|---|---:|---:|---:|---:|---|
| GPIO 10kΩ + ADC12 100kΩ | 24 | 115 | 120 | 122/123 | 四档稳定，可映射 |
| 仅 ADC12 100kΩ | 24 | 与 NONE 大量重叠在 32～34 | 与 NONE 大量重叠在 32～34 | 33 | ❌ 无法区分 PREV/NEXT/NONE |
| 仅 GPIO 10kΩ | 0 | 84 | 79～104，明显波动 | 92 | ❌ NEXT 与 PREV/NONE 重叠 |

### 7.2 选择结论

最终采用阶段一已验证的 **GPIO 10kΩ + ADC12 100kΩ双上拉**。另外两种组合均由实际硬件数据否决，不作为最终配置。

---

## 8. 阶段二：原始值到三键映射

### 8.1 键值定义

```c
#define KEY_NONE    0x00u
#define KEY_PLAY    0x01u
#define KEY_PREV    0x02u
#define KEY_NEXT    0x03u
#define KEY_UNKNOWN 0xffu
```

### 8.2 阈值来源

双上拉下实测稳定值为 PLAY=24、PREV=115、NEXT=120、NONE=122/123。PLAY/PREV 和 PREV/NEXT 使用相邻稳定值中点；NEXT/NONE 之间仅有 ADC=121，因此将 121 保留为死区，不强行映射。

| ADC12 原始值 | 映射结果 | 依据 |
|---:|---|---|
| 0～69 | PLAY / `0x01` | 24 与 115 的中点约为 69.5 |
| 70～117 | PREV / `0x02` | 115 与 120 的中点约为 117.5 |
| 118～120 | NEXT / `0x03` | NEXT 稳定值为 120 |
| 121 | UNKNOWN / `0xff` | NEXT/NONE 死区 |
| ≥122 | NONE / `0x00` | NONE 实测为 122/123 |

映射函数只完成单次 ADC 值分类，不包含消抖。按住时每 100ms 重复打印、按下或抬起瞬间出现中间状态，均属于本阶段预期行为。

### 8.3 用户实测输出

启动信息：

```text
[TEST] ADKEY stage 2: PB5 / ADC12 key mapping
[TEST] PB5 pull-up: GPIO 10K + ADC12 100K
[TEST] Map: <=69 PLAY, <=117 PREV, <=120 NEXT, 121 UNKNOWN, >=122 NONE
[TEST] No debounce yet: repeated lines and transition UNKNOWN are expected
```

四种稳定状态：

```text
# NONE
[TEST] ADC12 raw=123 -> key=NONE code=0x00

# PLAY
[TEST] ADC12 raw=24 -> key=PLAY code=0x01

# PREV
[TEST] ADC12 raw=115 -> key=PREV code=0x02

# NEXT
[TEST] ADC12 raw=120 -> key=NEXT code=0x03
```

实测中：

- 空闲期间连续输出 NONE，没有误报 NEXT；
- PLAY 持续按住时连续输出 PLAY；
- PREV 持续按住时连续输出 PREV；
- NEXT 持续按住时连续输出 NEXT，没有在 NEXT/NONE 之间跳变；
- 快速按下和松开时，键值与 NONE 按动作交替；
- 全程未出现 ADC 转换超时。

### 8.4 结论

✅ **阶段二通过**：三种板载按键均能由 ADC12 原始值正确映射到 `0x01/0x02/0x03`，抬起后恢复 `KEY_NONE=0x00`。NEXT/NONE 的原始值间隔虽小，但双上拉下持续状态稳定，且 121 已作为死区保留。

---

## 9. 阶段三：5ms 扫描消抖与短按/短抬

### 9.1 扫描时基

TMR0 已作为 main.c 的 1ms 系统中断，TMR2 已作为 1µs 延时基准，故按项目约束使用已经单独验证过的 TMR1：

```text
TMR1 输入时钟 = tmr_inc = 1MHz
TMR1PR = 5000 - 1
扫描周期 = 5000 × 1µs = 5ms
```

TMR1 ISR 只清除 `TMR1CPND[9]` 并递增扫描 tick。ADC12 转换、候选键统计、稳定状态更新和串口打印都在主循环完成，ISR 中不调用 `printf`。

### 9.2 连续采样消抖

```text
5ms/次 × 连续 5 次相同映射 = 25ms 消抖
```

状态处理规则：

1. 新采样键值与候选键不同：替换候选键，计数从 1 开始；
2. 与候选键相同：计数加 1，最大保持为 5；
3. 连续达到 5 次且候选键不同于稳定键：更新稳定键并产生边沿消息；
4. ADC=121 映射为 UNKNOWN：中断本次连续计数，不更新稳定状态；
5. 稳定键没有变化：不重复发送消息；
6. 按键直接从 A 切换到 B：稳定后先发送 A 的 SHORT_UP，再发送 B 的 SHORT。

主循环若因打印错过一个 TMR1 tick，只会延长实际消抖时间，不会把未采样的 tick 虚构成有效连续样本。

### 9.3 消息定义

```c
#define KEY_SHORT    0x0000u
#define KEY_SHORT_UP 0x0800u
```

| 按键 | 稳定按下 | 稳定抬起 |
|---|---:|---:|
| PLAY | `0x0001` | `0x0801` |
| PREV | `0x0002` | `0x0802` |
| NEXT | `0x0003` | `0x0803` |

本阶段尚未实现长按，因此即使持续按住，稳定按下后也只发送一次 SHORT；松开时仍发送 SHORT_UP。

### 9.4 用户实测流程

| 子测试 | 用户操作 | 期望 |
|---|---|---|
| Test 1 | PLAY/PREV/NEXT 各按一次 | 每键一对 SHORT + SHORT_UP，键值正确 |
| Test 2 | 每个键连续短按 10 次 | 每键正好 10 对消息，不多报、不漏报、不串键 |
| Test 3 | 每个键持续按住约 2 秒 | 按下只报一次，保持期间无重复，松开只报一次 |
| Test 4 | 制造不足 25ms 的极短脉冲 | 手动按键无法可靠产生，未执行，不作为阻塞项 |

Test 4 需要信号发生器或 GPIO 注入才能可重复验证。当前人工按键条件无法保证小于 25ms，因此如实记录为未执行；连续 5 次状态机逻辑、正常短按和持续按住行为已由其他测试覆盖。

### 9.5 用户实测输出

三个按键各按一次：

```text
[TEST] msg=0x0001 KEY_SHORT PLAY raw=24
[TEST] msg=0x0801 KEY_SHORT_UP PLAY raw=123
[TEST] msg=0x0002 KEY_SHORT PREV raw=115
[TEST] msg=0x0802 KEY_SHORT_UP PREV raw=123
[TEST] msg=0x0003 KEY_SHORT NEXT raw=120
[TEST] msg=0x0803 KEY_SHORT_UP NEXT raw=124
```

每键连续短按 10 次的统计：

| 按键 | SHORT 数量 | SHORT_UP 数量 | 错键/额外消息 |
|---|---:|---:|---:|
| PLAY | 10 | 10 | 0 |
| PREV | 10 | 10 | 0 |
| NEXT | 10 | 10 | 0 |

持续按住约 2 秒后，三个按键均只产生一对消息：

```text
[TEST] msg=0x0001 KEY_SHORT PLAY raw=24
[TEST] msg=0x0801 KEY_SHORT_UP PLAY raw=123
[TEST] msg=0x0002 KEY_SHORT PREV raw=115
[TEST] msg=0x0802 KEY_SHORT_UP PREV raw=123
[TEST] msg=0x0003 KEY_SHORT NEXT raw=120
[TEST] msg=0x0803 KEY_SHORT_UP NEXT raw=123
```

按住期间没有重复消息；全程没有 `KEY_UNKNOWN`、ADC 转换超时、漏报或串键。特别是 NEXT 连续短按 10 次全部得到 `0x0003/0x0803`，证明 NEXT=120 与 NONE=123 在 5次连续采样规则下可以稳定区分。

### 9.6 结论

✅ **阶段三通过**：TMR1 5ms 扫描正常；连续 5 次相同值的 25ms 按下/抬起消抖正常；PLAY/PREV/NEXT 的 SHORT 和 SHORT_UP 消息值、数量、顺序均正确。人工条件无法可靠制造不足 25ms 的物理脉冲，相关子测试记录为未执行，不影响当前硬件操作下的阶段验收。

---

## 10. 阶段四：700ms 长按与长抬

### 10.1 消息定义

```c
#define KEY_LONG     0x0a00u
#define KEY_LONG_UP  0x0c00u
```

完整消息值：

| 按键 | SHORT | SHORT_UP | LONG | LONG_UP |
|---|---:|---:|---:|---:|
| PLAY | `0x0001` | `0x0801` | `0x0a01` | `0x0c01` |
| PREV | `0x0002` | `0x0802` | `0x0a02` | `0x0c02` |
| NEXT | `0x0003` | `0x0803` | `0x0a03` | `0x0c03` |

### 10.2 计时基准

TMR1 5ms tick 仍然有效。长按阈值：

```c
#define ADKEY_LONG_MS    700u
#define ADKEY_LONG_TICKS ((ADKEY_LONG_MS * 1000u) / ADKEY_SCAN_PERIOD_US) // 140
```

`press_tick` 在每次稳定按下并发出 SHORT 的瞬间记录。LONG 触发条件：

```c
((u32)(current_tick - press_tick) >= ADKEY_LONG_TICKS)
```

无符号减法保证 2^32 × 5ms ≈ 68 年的 tick 计数回绕安全。

### 10.3 事件序列

| 阶段 | 操作 | 消息 |
|---|---|---|
| 1 | 稳定按下完成 25ms 消抖 | `KEY_SHORT \| key` |
| 2 | SHORT 之后约 700ms | `KEY_LONG \| key`（只发一次） |
| 3 | 700ms 之前稳定抬起 | `KEY_SHORT_UP \| key` |
| 4 | 700ms 之后稳定抬起 | `KEY_LONG_UP \| key` |

由于 `press_tick` 在 SHORT 发送瞬间记录，从物理触碰到 LONG 实际约为 25ms（消抖）+ 700ms = 约 725ms。

### 10.4 用户实测

#### Test 1：三个键各短按一次

[adc.txt:6-11](test/adc.txt#L6-L11)：

```text
PLAY: 0x0001 → 0x0801 (held=145 ms)
PREV: 0x0002 → 0x0802 (held=150 ms)
NEXT: 0x0003 → 0x0803 (held=125 ms)
```

每个键没有出现 LONG/LONG_UP。

#### Test 2：三个键各长按约 0.8～1.3 秒

[adc.txt:12-20](test/adc.txt#L12-L20)：

```text
PLAY: 0x0001 → 0x0a01 (held=700 ms) → 0x0c01 (held=795 ms)
PREV: 0x0002 → 0x0a02 (held=700 ms) → 0x0c02 (held=1315 ms)
NEXT: 0x0003 → 0x0a03 (held=700 ms) → 0x0c03 (held=820 ms)
```

LONG 的 `held=700 ms` 与阈值一致；松开时 LONG_UP 的 `held` 等于从 SHORT 到抬起的总时长减去 25ms 消抖。

#### Test 3：三个键各长按约 2.5～3.8 秒

[adc.txt:21-29](test/adc.txt#L21-L29)：

```text
PLAY: 0x0001 → 0x0a01 → 0x0c01 (held=3835 ms)
PREV: 0x0002 → 0x0a02 → 0x0c02 (held=2735 ms)
NEXT: 0x0003 → 0x0a03 → 0x0c03 (held=2565 ms)
```

每个键在 700ms 之后保持期间没有新消息。

#### Test 4：短按/长按交替

[adc.txt:30-44](test/adc.txt#L30-L44)：

```text
PLAY 短按: 0x0001 → 0x0801 (held=155 ms)
PLAY 长按: 0x0001 → 0x0a01 → 0x0c01 (held=1555 ms)
PREV 短按: 0x0002 → 0x0802 (held=165 ms)
PREV 长按: 0x0002 → 0x0a02 → 0x0c02 (held=1525 ms)
NEXT 短按: 0x0003 → 0x0803 (held=110 ms)
NEXT 长按: 0x0003 → 0x0a03 → 0x0c03 (held=1900 ms)
```

每个键的 SHORT_UP 和 LONG_UP 不会混淆。

### 10.5 结论

✅ **阶段四通过**：

- SHORT 在 25ms 消抖完成时发送；
- LONG 在 SHORT 之后 700ms 发送，且只发送一次；
- 700ms 前松开发 SHORT_UP；
- LONG 后松开发 LONG_UP；
- 长时间按住期间没有重复消息；
- NEXT 同样能正确识别 LONG/LONG_UP，未受阈值死区影响；
- 没有 ADC 转换超时、串键或事件重复。

P/P 长按测试中实际按住达到约 3.8 秒，没有触发 PB5 的 10S Reset 复位，符合芯片 10 秒阈值。

---

## 11. 阶段五：200ms HOLD 连发

### 11.1 消息定义

```c
#define KEY_HOLD 0x0e00u
```

完整消息值：

| 按键 | SHORT | SHORT_UP | LONG | LONG_UP | HOLD |
|---|---:|---:|---:|---:|---:|
| PLAY | `0x0001` | `0x0801` | `0x0a01` | `0x0c01` | `0x0e01` |
| PREV | `0x0002` | `0x0802` | `0x0a02` | `0x0c02` | `0x0e02` |
| NEXT | `0x0003` | `0x0803` | `0x0a03` | `0x0c03` | `0x0e03` |

### 11.2 计时基准

TMR1 5ms tick 仍然有效。HOLD 周期：

```c
#define ADKEY_HOLD_MS    200u
#define ADKEY_HOLD_TICKS ((ADKEY_HOLD_MS * 1000u) / ADKEY_SCAN_PERIOD_US) // 40
```

LONG 之后由 `long_tick` 维护上一次 LONG/HOLD 时刻，每次发出 HOLD 后立即更新为当前 tick：

```c
if (long_sent && ((u32)(current_tick - long_tick) >= ADKEY_HOLD_TICKS)) {
    ...
    long_tick = current_tick;
    TEST_LOG("msg=0x%04x KEY_HOLD ...", ...);
}
```

`held` 累计时长继续基于 `press_tick`，因此 HOLD 不会改变总按压时长的语义。LONG 之前松开发 SHORT_UP，LONG 之后松开发 LONG_UP；松开时 HOLD 立即停止。

### 11.3 事件序列

| 阶段 | 操作 | 消息 |
|---|---|---|
| 1 | 稳定按下完成 25ms 消抖 | `KEY_SHORT \| key` |
| 2 | SHORT 之后约 700ms | `KEY_LONG \| key`（只发一次） |
| 3 | LONG 之后每 200ms | `KEY_HOLD \| key` |
| 4 | 700ms 之前稳定抬起 | `KEY_SHORT_UP \| key` |
| 5 | 700ms 之后稳定抬起 | `KEY_LONG_UP \| key`（HOLD 立即停止） |

### 11.4 用户实测

#### Test 1：三个键各短按一次

[adc.txt:1-6](test/adc.txt#L1-L6)：

```text
PLAY: 0x0001 -> 0x0801 (held=75 ms)
PREV: 0x0002 -> 0x0802 (held=95 ms)
NEXT: 0x0003 -> 0x0803 (held=75 ms)
```

未出现 LONG/HOLD/LONG_UP。

#### Test 2：长按约 0.7～1.0 秒

[adc.txt:7-15](test/adc.txt#L7-L15)：

```text
PLAY 695 ms:  0x0001 -> 0x0801
PREV 780 ms:  0x0002 -> 0x0a02 (held=700 ms) -> 0x0c02
NEXT 990 ms:  0x0003 -> 0x0a03 (held=700 ms) -> 0x0e03 (held=900 ms) -> 0x0c03
```

PLAY 实际只按了 695ms，消抖后约 670ms，没有达到 700ms 阈值，行为正确；PREV 按了 780ms，触发 LONG；NEXT 按了 990ms，LONG 后发出 1 条 HOLD 后松开。

#### Test 3：长按约 1.5～1.9 秒

[adc.txt:16-22](test/adc.txt#L16-L22)：

```text
PLAY 1605 ms: 0x0001 -> 0x0a01 (held=700 ms)
                0x0e01 (held=900 ms)  0x0e01 (held=1100 ms)
                0x0e01 (held=1300 ms) 0x0e01 (held=1500 ms)
                0x0c01 (held=1605 ms)
```

HOLD 间隔恰好 200ms。

#### Test 4：长按约 1.6～2.3 秒

[adc.txt:23-37](test/adc.txt#L23-L37)：

```text
PREV 1655 ms: 0x0002 -> 0x0a02 -> 0x0e02 × 4 -> 0x0c02
NEXT 1875 ms: 0x0003 -> 0x0a03 -> 0x0e03 × 5 -> 0x0c03
```

#### Test 5：长按约 2.0～2.3 秒

[adc.txt:38-65](test/adc.txt#L38-L65)：

```text
PLAY 1960 ms: 0x0001 -> 0x0a01 -> 0x0e01 × 6 -> 0x0c01
PREV 2320 ms: 0x0002 -> 0x0a02 -> 0x0e02 × 8 -> 0x0c02
NEXT 1985 ms: 0x0003 -> 0x0a03 -> 0x0e03 × 6 -> 0x0c03
```

每条 HOLD 间隔均接近 200ms；松开时立刻得到 LONG_UP，不会再出现 HOLD。

#### Test 6：松开瞬间抖动到 PLAY 边界

[adc.txt:66-69](test/adc.txt#L66-L69)：

```text
PLAY 925 ms:  0x0001 -> 0x0a01 -> 0x0e01 (raw=27) -> 0x0c01
```

HOLD 采样到 `raw=27`（仍在 PLAY 阈值 ≤69 内），状态机识别为 PLAY，未触发 UNKNOWN/HOLD 中断。

#### Test 7：松开后立即短按 PREV

[adc.txt:70-71](test/adc.txt#L70-L71)：

```text
PREV 130 ms:  0x0002 -> 0x0802
```

完整短按/短抬消息顺序正确，与阶段三、四一致。

### 11.5 阶段五结论

✅ **阶段五通过**：

- LONG 在 700ms 时只发送一次；
- LONG 之后每 200ms 重复一次 HOLD；
- 松开发 LONG_UP 而不是 SHORT_UP；
- 短按/长按/连发事件值与顺序均正确；
- NEXT 与 NONE 原始值相近，HOLD 期间仍能稳定识别，没有误触 UNKNOWN 或漏报 HOLD；
- 整个过程中没有 ADC 转换超时、串键或事件重复。

至此阶段一～五全部通过：ADKEY 原始采样、双上拉选择、三键映射、25ms 消抖、700ms 长按/长抬、200ms HOLD 连发。

---

## 12. 阶段五开发过程中的链接错误及修复

### 12.1 现象

最初启用 `TEST_ADKEY_HOLD_EN` 后，Build 报告：

```text
riscv32-elf-ld.exe: section .comm VMA [0000000000011000,0000000000019013]
riscv32-elf-ld.exe: region `comm' overflowed by 20 bytes
Process terminated with status 1
0 error(s), 1 warning(s)
```

Downloader 烧录时拿到的 `app.dcf` 仍是 `7bc4c21` 的产物，串口启动信息仍然显示 `stage 4`，且串口助手收到的全部是 stage 4 的 SHORT/LONG/LONG_UP 消息，**看不到任何 HOLD 消息**。

### 12.2 根因

`ram.ld` 把所有 test 代码放进 `.comm` 段，地址范围 0x11000 ~ 0x19000（32kB）：

```ld
__comm_vma = 0x11000;
__max_comm_size = 32k;
...
.comm : {
    *(.vector)
    *(.com_text*)
    *(.com_rodata*)
    ...
} > comm AT > flash
.bss (NOLOAD): {
    __bss_start = .;
    *(.bss)
    ...
} > data
```

阶段四 + 阶段五把 `test_adkey_long_run`（含 100 多行状态机）以及新加的 `test_adkey_hold_run`（又一个完整状态机）链接进同一个 `.comm` 段，使 `.comm` 实际结束地址达到 0x19013，比预留上限 0x19000 多了 20 字节，触发 20 字节溢出。

链接失败时 CodeBlocks 不会运行 `postbuild.bat`，因此 `app.dcf` 不会刷新，开发板上运行的仍是上一版阶段四的固件。

### 12.3 修复

`test_adkey_long_run` 的全部 SHORT/LONG/LONG_UP 行为已经在 `test_adkey_hold_run` 中完整实现（`test_adkey_hold_run` 是阶段四行为 + HOLD 的合并）。本版本将 `test_adkey_long_run` 替换为最小引导实现：

```c
void test_adkey_long_run(void)
{
    TEST_LOG("ADKEY stage 4 has been merged into stage 5 (HOLD)");
    TEST_LOG("Please enable TEST_ADKEY_HOLD_EN instead");
    while (1) {
        delay_ms(1000);
    }
}
```

这样阶段四的状态机代码（100 多行）不再链接进 `.comm`，约 110 字节空间被释放，溢出 20 字节的差值被覆盖，链接成功。

由于当前只启用 `TEST_ADKEY_HOLD_EN`，`test_adkey_long_run` 不会执行；阶段四功能只通过 `test_adkey_hold_run` 演示，测试入口名称不变，已在测试报告 §0 中如实记录。

### 12.4 验证

修复后链接器输出 `Output\bin\app.rv32` 成功，`postbuild.bat` 重新生成 `Output\bin\app.dcf`。烧录新固件后串口首行变为：

```text
[TEST] ADKEY stage 5: 200ms HOLD repeat
[TEST] Events: SHORT -> LONG at 700ms -> HOLD every 200ms -> LONG_UP on release
```

长按 PLAY 约 2 秒得到 `0x0001 → 0x0a01 → 0x0e01 × N → 0x0c01`，符合阶段五预期。

### 12.5 教训

`.comm` 段 32kB 容量随测试代码增长，**链接失败时必须检查链接器输出，而不是只看 CodeBlocks 顶部提示**。新增测试入口时建议确认 `app.rv32` 中 `.comm` 实际使用率，必要时精简冗余状态机或将其合并到后续阶段。
