# BT892X ADKEY 测试报告

> **测试日期**：2026-07-20
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考资料**：
> - [SARADC_CTL（逐次逼近型 ADC）数据手册摘要](SARADC_CTL%20%28逐次逼近型%20ADC%29%20数据手册摘要.md)
> - [bt892x_pinfunction.md §4.2 PORTB / §8.10 SARADC](bt892x_pinfunction.md)
> - [BT892X_UserManual_Driver.md §3 GPIO](BT892X_UserManual_Driver.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **当前阶段**：阶段一——PB5/ADC12 原始值采集与串口打印
> **测试结果**：✅ ADC12 转换、按键响应和四档稳定原始值均通过；⚠️ NEXT 与 NONE 仅相差 2 LSB，键值映射阶段需继续验证裕量

---

## 0. 文档结构

| 程序 | 路径 | 入口/开关 | 用途 |
|---|---|---|---|
| **test_adkey.c** | `smart_mini/test/test_adkey.c` | `test_adkey_raw_run` / `TEST_ADKEY_RAW_EN` | 初始化 PB5/ADC12，每 100ms 采样并打印原始值 |
| **test_adkey.h** | `smart_mini/test/test_adkey.h` | — | 测试入口声明 |

本阶段只验证原始 ADC 采集，不加入键值阈值、5ms 扫描消抖、短按、长按或连发状态机。

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

## 7. 下一阶段

阶段二先根据真实 ADC 数据建立 `KEY_NONE/KEY_PLAY/KEY_PREV/KEY_NEXT` 映射，并验证 NEXT/NONE 裕量；映射确认后再进入 TMR1 5ms 扫描和连续 5 次相同值的消抖测试。
