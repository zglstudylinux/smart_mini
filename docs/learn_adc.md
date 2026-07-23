# ADC 学习笔记（结合 BT892X ADKEY 测试示例）

> **作者前置**：本笔记面向**第一次接触 ADC** 的读者。如果你学过数字电路但没摸过模数转换，跟着读就能建立完整直觉。
> **目标芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）—— 自带 **16 通道 10-bit SARADC**
> **配套代码**：[smart_mini/test/test_adkey.c](../smart_mini/test/test_adkey.c)
> **配套数据手册**：[SARADC_CTL（逐次逼近型 ADC）数据手册摘要](SARADC_CTL%20%28逐次逼近型%20ADC%29%20数据手册摘要.md)
> **配套测试报告**：[docs/test_adkey.md](test_adkey.md)（实战现象 / 阈值 / 消抖 / 长按 / 连发）
> **学习路径**：概念 → 原理 → BT892X SARADC 实战 → 阅读 test_adkey.c → 自己写个 ADC 测试

---

## 0. 阅读路线

| 章 | 主题 | 学完后能回答 |
|---|---|---|
| §1 | ADC 是什么、用在哪 | "ADC 把电压变成数字，具体怎么变？" |
| §2 | SAR ADC 工作原理（二分搜索） | "为什么叫'逐次逼近'？" |
| §3 | 关键参数：分辨率 / 采样率 / 通道 / 输入阻抗 | "10-bit 精度够用吗？" |
| §4 | BT892X SARADC 硬件架构 | "手册上 16 个通道怎么接的？" |
| §5 | 从零配置 SARADC（照着手册抄） | "我要写 `SADCCON = ...`，每个 bit 什么意思？" |
| §6 | 跑一次 ADC 转换的完整流程 | "为什么写 `SADCCH=BIT(12)` 就开始转换了？" |
| §7 | ADKEY 实例：从原始值到按键识别 | "原始值 24/115/120/122 是怎么映射到 PLAY/PREV/NEXT/NONE 的？" |
| §8 | 进阶：消抖 / 长按 / 连发 | "按键怎么才能像手机一样好用？" |
| §9 | 调试与排错 | "为什么一直读到 0 / 一直读到 1023？" |
| §10 | 练习题 | "不看代码自己写个 ADC 读温度" |

---

## 1. ADC 是什么？

### 1.1 一句话定义

**ADC（Analog-to-Digital Converter）= 把连续变化的电压值，变成离散数字代码的电路。**

```
                  ADC
  Vin (0~3.3V)  ─────►  Dout (10-bit, 0~1023)
  [模拟世界]                  [数字世界]
```

### 1.2 为什么要 ADC？

| 场景 | 模拟量 | 数字量 |
|---|---|---|
| 温度传感器 | 0~3.3V 电压 | 0~1023 数值 |
| 按键电阻分压 | 不同电压 | 不同按键 |
| 麦克风 | 连续声波 | 音频采样流 |
| 电池电量 | VBAT 电压 | 百分比 |
| 光敏电阻 | 亮度→电压 | 亮度等级 |

> 关键观察：**现实世界是连续的**（温度从 25.001℃ 到 25.002℃ 是连续的），**MCU 只能处理离散的数字**。ADC 就是那座桥。

### 1.3 跟 DAC 的区别

- **ADC** = Analog → Digital（采样、量化）
- **DAC** = Digital → Analog（重建波形，比如音频输出）
- 两者经常成对出现（音频编解码器 ADC+DAC）

---

## 2. SAR ADC 工作原理（二分搜索）

BT892X 用的是 **SAR（Successive Approximation Register，逐次逼近型）ADC**。这个名字听起来吓人，但原理就一句话：**用二分搜索猜电压**。

### 2.1 直观比喻：猜数字游戏

> 主持人心里想一个 0~1023 的数字。你每次猜一个数字，主持人说"大了"或"小了"。**最快 10 次**就能猜中（因为 2^10 = 1024）。

SAR ADC 的工作一模一样：

1. **第 1 次**：DAC 输出 512（即 bit 9 = 1）→ 比较 Vin > 512？
   - 若 Vin > 512：bit 9 = 1（保留）
   - 否则：bit 9 = 0
2. **第 2 次**：根据 bit 9 决定，再试 bit 8（叠加 256 或 0）
3. ...
4. **第 10 次**：试 bit 0（叠加 1）

每一步都把搜索区间**减半**，所以叫"逐次逼近"。10-bit ADC 需要 **10 次比较 + 1 次保持 = 11 个时钟周期** 完成一次转换。

### 2.2 内部结构

```
                ┌─────────────┐
   Vin ────►────┤  Sample &    ├────┐
                │   Hold (S&H) │    │
                └─────────────┘    │
                                   ▼
                              ┌────────┐    ┌──────┐
                              │比较器  │◄───┤ DAC  │◄── SAR 寄存器
                              │ Vin ?  │    │(猜测)│    逐位设 1
                              │ Vdac   │    └──────┘
                              └────────┘
                                   │
                                   ▼ 大/小
                              SAR 寄存器更新
```

### 2.3 为什么用 SAR？

| 类型 | 优点 | 缺点 | 典型用途 |
|---|---|---|---|
| **SAR** | 速度/精度平衡好、面积小、功耗低 | 高速场景比不上 Flash/流水线 | **通用 MCU**、按键、传感器 |
| Flash | 极快（一次比较出所有位） | 精度上不去（一般 8-bit）、面积大 | 高速示波器、SDR |
| Sigma-Delta | 精度极高（16~24-bit） | 速度慢 | 音频、精密测量 |
| Dual-Slope | 抗干扰强 | 速度很慢 | 数字万用表 |

**结论**：嵌入式 MCU 里 95% 的场景用 SAR。BT892X 选 SAR 是典型做法。

---

## 3. 关键参数

### 3.1 分辨率（Resolution）

```
分辨率 = 输出位数（如 10-bit）
代码范围 = 0 ~ (2^N - 1) = 0 ~ 1023 (10-bit)
LSB = Vref / 2^N = 3.3V / 1024 ≈ 3.22 mV（1 个 LSB 对应的电压）
```

**BT892X 是 10-bit**：原始值范围 0~1023，每个 LSB 约 3.22 mV。

**够用吗？**
- 按键电压间隔几百 mV → 10-bit 绰绰有余
- 电池电压精度 1% → 10-bit 略紧
- 精密测量温度 ±0.1℃ → 不够，需要 16-bit+

### 3.2 采样率（Sampling Rate）

**SARADC 最大 78 kS/s**（每秒 7.8 万次）。

> 这对按键扫描来说太奢侈了。我们用 100 ms / 次 = 10 Hz。

### 3.3 通道数（Channels）

**BT892X 有 16 个通道**（ADC0 ~ ADC15），可以接 16 路模拟输入。

引脚映射（部分）：

| ADC 通道 | 引脚 | 备注 |
|---|---|---|
| ADC0 | PA5 | |
| ADC1 | PA6 | |
| ADC2 | PA7 | |
| ADC3 | PB1 | |
| ADC4 | PB2 | |
| ADC5 | PB3 | |
| ADC6 | PB4 | |
| **ADC12** | **PB5** | **10S Reset 主唤醒源；本测试就用它** |

> **重要约束**：一个 ADC 同一时刻只能转换 1 个通道！多通道是**分时复用**的（轮询扫描）。

### 3.4 输入阻抗 / 采样保持

SAR ADC 内部有个**采样电容**。转换开始时电容接到 Vin，要花时间充电到 Vin 电平。这个时间叫**采样建立时间**（Setup Time）。

- 如果信号源阻抗低（< 10 kΩ）：建立时间可忽略
- 如果信号源阻抗高（> 100 kΩ）：需要更长建立时间，否则读数偏低

BT892X `SADCST` 寄存器可以设通道建立时间（0/2/4/8 个 SARADC 时钟），默认 0 实测够用。

### 3.5 参考电压（Vref）

```
Dout = (Vin / Vref) × 2^N
```

BT892X **Vref 默认 = VDDIO = 3.3V**（片内固定，不能选外部 Vref）。

---

## 4. BT892X SARADC 硬件架构

### 4.1 时钟树

```
                ┌─────────┐
                │ x24m    │ 24 MHz 晶振
                └────┬────┘
                     ▼ clkdiv4
                ┌─────────┐
                │x24m_clk │ 6 MHz
                │  div4   │
                └────┬────┘
                     ▼
                ┌─────────┐  CLKCON0[28]=1 选这条
                │ CLKCON0 │
                │   [28]  │ CLKCON0[28]=0 时选 rc2m_clk（内部 2 MHz RC）
                └────┬────┘
                     ▼
                ┌─────────┐
                │ CLKGAT0 │
                │   [13]  │ SARADC 时钟门（必须开！）
                └────┬────┘
                     ▼
                ┌─────────┐
                │ Fadc    │ ADC 时钟
                │  clock  │
                └────┬────┘
                     ▼
                SADCBAUD 分频器
                     ▼
                ┌─────────┐
                │SARADC_CLK│ 位时钟，送给内部比较器
                └─────────┘
```

### 4.2 寄存器清单（手册 §11.2）

| 寄存器 | 作用 | 关键位 |
|---|---|---|
| `SADCCON` | SARADC 控制 | [19] ADCAEN / [18] ADCANGIO / [17] ADCIE / [16] ADCEN / [12] CH12PUEN |
| `SADCCH` | 通道使能 + 状态 | [16] ADCPND / [15:0] CHnEN |
| `SADCST` | 通道建立时间 | [1:0] CH0ST / [3:2] CH1ST / ... |
| `SADCBAUD` | 波特率分频 | [9:0] SADCBAUD |
| `SADCDAT0~15` | 各通道数据 | [9:0] SADCDAT（10-bit 结果） |

### 4.3 内部 100 kΩ 上拉

SARADC 每个通道都内置一个 **100 kΩ 上拉电阻**，可由 `SADCCON[CHnPUEN]` 独立使能。

> **为什么要这个上拉**：ADKEY 这种电阻网络，按键抬起时需要一条"高电平通路"，否则悬空状态读数会漂。100 kΩ 内部上拉完美解决：节省 PCB 空间 + 软件可控。

---

## 5. 从零配置 SARADC（照着手册抄）

看 [test_adkey.c:51-79](../smart_mini/test/test_adkey.c#L51-L79) 的初始化函数：

```c
static void test_adkey_raw_init(void)
{
    // ===== 第 1 步：开时钟 =====
    // 手册 §11.2：CLKCON0[28]=1 选 x24m_clkdiv4 (6MHz)
    CLKCON0 |= SARADC_CLK_SEL_X24M_DIV4;     // BIT(28)
    // 手册 §11.2：CLKGAT0[13]=1 开 SARADC 时钟门
    CLKGAT0 |= SARADC_CLK_GATE;              // BIT(13)

    // ===== 第 2 步：PB5 引脚配成普通 GPIO 输入 =====
    GPIOBDIR    |=  ADKEY_PIN_MASK;          // DIR=1 输入
    GPIOBDE     |=  ADKEY_PIN_MASK;          // DE=1 数字 IO
    GPIOBFEN    &= ~ADKEY_PIN_MASK;          // FEN=0 不映射到其它外设
    GPIOBPU     |=  ADKEY_PIN_MASK;          // 10kΩ 上拉
    // 关闭其它上拉/下拉避免干扰
    GPIOBPD     &= ~ADKEY_PIN_MASK;
    GPIOBPU200K &= ~ADKEY_PIN_MASK;
    GPIOBPD200K &= ~ADKEY_PIN_MASK;
    GPIOBPU300  &= ~ADKEY_PIN_MASK;
    GPIOBPD300  &= ~ADKEY_PIN_MASK;

    // ===== 第 3 步：配波特率 =====
    // 公式：SARADC_CLK = Fadc_clock / [2 × (SADCBAUD + 1)]
    // 6MHz / [2 × (5 + 1)] = 500 kHz
    SADCBAUD = SARADC_BAUD_500KHZ;           // = 5

    // ===== 第 4 步：建立时间保持默认 =====
    SADCST = 0;                              // 所有通道 0 个 SARADC_CLK

    // ===== 第 5 步：开 SARADC =====
    // ADCAEN=1 自动使能模拟模块
    // ADCANGIO=1 自动使能模拟 IO
    // ADCEN=1 使能 SARADC
    // CH12PUEN=1 使能 ADC12 内部 100 kΩ 上拉
    SADCCON = SARADC_AUTO_ANALOG_EN          // BIT(19)
            | SARADC_AUTO_ANALOG_IO_EN       // BIT(18)
            | SARADC_ADC_EN                  // BIT(16)
            | SARADC_CH12_PULLUP_EN;         // BIT(12)
}
```

### 5.1 逐行解释

#### 第 1 步：开时钟

```c
CLKCON0 |= BIT(28);  // 选 6MHz 时钟源
CLKGAT0 |= BIT(13);  // 开时钟门
```

> **为什么先开时钟**：MCU 外设默认都是关闭的（省电）。不打开时钟门，寄存器读写是无效的。
> **类比**：就像给电风扇接电——先开插座的开关（CLKCON0），再按风扇的电源键（CLKGAT0）。

#### 第 2 步：PB5 配 GPIO

```c
GPIOBPU    |=  BIT(5);  // GPIO 10 kΩ 上拉
// 注意 SARADC 也有 100 kΩ 上拉（CH12PUEN）
// 双上拉并联 ≈ 9.1 kΩ，详见 §5.2
```

> **为什么 GPIO 也要上拉**：手册允许 3 种上拉组合（仅 GPIO / 仅 ADC / 双上拉）。双上拉是阶段二实测验证的最佳组合。

#### 第 3 步：分频到 500 kHz

公式：`SARADC_CLK = Fadc_clock / [2 × (SADCBAUD + 1)]`

```text
SARADC_CLK = 6 MHz / [2 × (5 + 1)] = 500 kHz
```

> **为什么 500 kHz**：手册规定 ≤ 1 MHz。500 kHz 留足余量，且转换时间（约 11 × 2µs = 22 µs）足够快。

#### 第 4 步：建立时间

```c
SADCST = 0;  // 0 个 SARADC_CLK
```

> **什么是建立时间**：S&H（采样保持）电容从引脚电压充到稳定值的时间。信号源阻抗越大，需要越长。本测试按键电阻最大 47 kΩ + 双上拉 9.1 kΩ = 实测 0 已够。

#### 第 5 步：开 ADC

4 个 bit 缺一不可：
- **ADCAEN** (BIT 19)：自动使能 SARADC 模拟模块
- **ADCANGIO** (BIT 18)：自动使能模拟 IO（PB5 当 GPIO 时其它外设不会干扰）
- **ADCEN** (BIT 16)：SARADC 主开关
- **CH12PUEN** (BIT 12)：ADC12 内部 100 kΩ 上拉

> ⚠️ **常见错误**：漏写 ADCAEN/ADCANGIO，ADC 能跑但读数不稳。阶段二实测已修复。

### 5.2 双上拉等效电阻

GPIO 10 kΩ 与 ADC 100 kΩ **并联**：

```
R_parallel = (10 × 100) / (10 + 100) = 1000 / 110 ≈ 9.09 kΩ
```

**理论分压公式**（假设 ADKEY 按下=把 PB5 短路到 GND）：

```
V_PB5 = VDD × R_key / (R_key + 9.09k)
```

按此公式可算出"理论 ADC"：

| 状态 | R_key | 理论 V_PB5 | 理论 ADC |
|---|---|---:|---:|
| 抬起 | ∞ (开路) | VDD (3.3V) | 1023 |
| P/P | 0 Ω | 0 V | 0 |
| PREV | 12 kΩ | 1.88 V | ~583 |
| NEXT | 47 kΩ | 2.77 V | ~859 |

> ⚠️ **这只是简化假设下的"理论值"**——真实板载 ADKEY 电路还有未公开的外部分压电阻，**实测值与理论值差距很大**。详见 §7.2 的实测表 与 §7.6 的"理论 vs 实测差距分析"。本节先建立"分压电路 → 电压 → ADC"的链路直觉，**不要用这里的数字去校准**。

---

## 6. 跑一次 ADC 转换的完整流程

### 6.1 完整代码（test_adkey.c L81-97）

```c
static bool test_adkey_raw_read(u32 *raw)
{
    u32 start;

    // ===== 启动转换 =====
    // 写 SADCCH=BIT(12) → 自动清 ADCPND + 启动 ADC12 转换
    SADCCH = SARADC_CH12_EN;
    start = tick_get();

    // ===== 等待完成（轮询 ADCPND）=====
    while (!(SADCCH & SARADC_ADCPND)) {
        if (tick_check_expire(start, SARADC_TIMEOUT_US)) {
            return false;  // 超时 5 ms
        }
    }

    // ===== 读取结果 =====
    *raw = SADCDAT12 & SARADC_DATA_MASK;  // [9:0]
    return true;
}
```

### 6.2 三步详解

#### 步骤 A：启动

```c
SADCCH = BIT(12);
```

手册原话："写操作会自动清除 ADCPND 标志并启动 ADC 转换"。

> **关键洞察**：**写寄存器 = 触发动作**。这不是普通的"赋值"，而是命令。
> 类比：写 UART0DATA 是触发发送，写 SADCCH 是触发采样。

#### 步骤 B：轮询

```c
while (!(SADCCH & BIT(16))) {  // ADCPND = BIT(16)
    // 等...
}
```

`ADCPND` 是 **Auto-Clear by Read** 的"完成标志"——转换完成后硬件置 1，软件读或写 `SADCCH` 会清零（所以下一次写 `SADCCH = BIT(12)` 同时干了两件事：清 ADCPND + 启动新转换）。

#### 步骤 C：读数据

```c
*raw = SADCDAT12 & 0x3FF;  // 0x3FF = 10-bit mask
```

> **为什么 `& 0x3FF`**：SADCDAT 是 32-bit 寄存器，但只有低 10 位有效，高位是噪声/旧值，必须 mask 掉。

### 6.3 时序图

```
  写 SADCCH=BIT(12)              ADCPND=1
       │                              │
       ▼                              ▼
  ─────┬──────────────────────────────┬───
       │  ⏱ ~22 µs (500kHz × 11周期)   │
       │                              │
     启动转换                       完成
       ↓                              ↓
   清 ADCPND                      置 ADCPND
   开始 S&H                       比较完成
   开始逐次逼近                   10-bit 结果就绪
```

500 kHz × 11 cycles = **22 µs 一次转换**。

### 6.4 超时保护

```c
if (tick_check_expire(start, SARADC_TIMEOUT_US)) {
    return false;
}
```

5 ms 没完成就放弃（避免程序卡死）。如果频繁 timeout，通常是 §9 的几个常见错误。

---

## 7. ADKEY 实例：从原始值到按键识别

### 7.1 硬件电路

```
                  PB5/ADC12
                     │
                     ├──── 100 kΩ 内部上拉 (CH12PUEN)
                     │
                     ├──── 10 kΩ  GPIO 上拉 (GPIOBPU)
                     │
                  ┌──┴──┐
                  │ KEY │  PWRKEY 节点
                  └──┬──┘
        ┌────────────┼────────────┐
        │            │            │
       P/P         PREV         NEXT
       0Ω          12k          47k
        │            │            │
       GND          GND          GND
```

### 7.2 实测稳定值（test_adkey.md §4.3）

| 状态 | 标称电阻 | 实测 ADC | 简化电路假设下的"理论" |
|---|---|---:|---|
| **PLAY** (P/P) | 0 Ω | **24** | 0 |
| **PREV** | 12 kΩ | **115** | ~583 |
| **NEXT** | 47 kΩ | **120** | ~859 |
| **NONE** | ∞ | **122~123** | 1023 |

> ⚠️ **表格里"理论"那列只是简化计算**——基于 3.3V × R_key / (R_key + 9.09k) × 1023 得到。
> **实测与理论差距很大**（10 倍量级），原因是真实电路还有板上未公开的外部分压电阻，不是 §7.1 画的简化模型。
> 详细反推见 §7.6，下面只先看**相对关系**是对的：NONE 最大、PLAY 最小，按键之间能区分。
>
> **为什么实测比理论小**：实际 VDD 不到 3.3V + GPIO 上下拉电阻公差 + 内部电阻精度 + 板上有未公开的分压网络。
> **为什么 NONE 不是 1023**：片内上拉不够强 + 板上分压网络使 PB5 永远拉不到顶 + 漏电流/寄生电阻。

### 7.3 阈值表（test_adkey.c L36-41）

```c
#define ADKEY_PLAY_MAX   69u   // PLAY/PREV 中点（(24+115)/2 ≈ 69）
#define ADKEY_PREV_MAX   117u  // PREV/NEXT 中点（(115+120)/2 ≈ 117）
#define ADKEY_NEXT_MAX   120u  // NEXT 稳定值（直接用实测中心）
#define ADKEY_NONE_MIN   122u  // NONE 实测下限
```

**映射规则**：

| ADC 原始值 | 映射键 |
|---|---|
| 0 ~ 69 | **PLAY** |
| 70 ~ 117 | **PREV** |
| 118 ~ 120 | **NEXT** |
| **121** | **UNKNOWN**（死区！） |
| ≥ 122 | **NONE** |

> **为什么要 121 死区**：NEXT=120 与 NONE=122 只差 2 LSB，万一读数抖到 121 是测不准的"模糊地带"。**承认不知道**，比**瞎猜**更稳。

### 7.4 映射函数（test_adkey.c L147-162）

```c
static u8 test_adkey_map_raw(u32 raw)
{
    if (raw <= ADKEY_PLAY_MAX)  return KEY_PLAY;
    if (raw <= ADKEY_PREV_MAX)  return KEY_PREV;
    if (raw <= ADKEY_NEXT_MAX)  return KEY_NEXT;
    if (raw >= ADKEY_NONE_MIN)  return KEY_NONE;
    return KEY_UNKNOWN;          // 121 死区
}
```

### 7.5 一次完整的按键动作序列

```
用户按下 P/P (PLAY):
  ──────────────────────────────────────────►  时间
       NONE(122) → 短脉冲(70~120) → PLAY(24)
       ↑                                       ↑
   ADC 读数                              持续按住期间

阶段二单次映射输出:
  ADC12 raw=122 -> key=NONE
  ADC12 raw=120 -> key=NEXT    ← 抖动或边界值
  ADC12 raw=24  -> key=PLAY    ← 稳定后
  ADC12 raw=24  -> key=PLAY    ← 持续按住期间每 100ms 重复
  ADC12 raw=24  -> key=PLAY
  ADC12 raw=122 -> key=NONE    ← 松开恢复 NONE
```

> 阶段二的"每 100ms 重复打印"是测试用——真实产品**不需要这种噪声**。这就引出 §8 的消抖。

### 7.6 理论值 vs 实测值：差距从哪儿来？

如果你对上面的表感到困惑——"理论 ADC=583 实测只有 115，这差了好几倍是怎么回事？"——这一节专门回答。

#### 7.6.1 重新审视硬件电路

§7.1 画的简化电路其实是**理想化**的。真实电路需要重新读 [test_adkey.md §1.2](../docs/test_adkey.md)：

> "PB5 使用 GPIO 10kΩ上拉，同时使能 ADC12 内部 100kΩ上拉。**三个按键按下后形成不同分压**，由 ADC12 转换为原始数字值。"

注意：**"按下后形成不同分压"**——按键一端接 PB5，另一端接什么？

如果只是 **按键 → GND**（按下=接地），那 V_PB5 应该 0~3.3V 之间分压。**但实测 NONE=122/1023 ≈ 12%**，这意味着即使没按键，PB5 也只拉到 12% 的 VDD——**说明"上拉到 VDD"很弱**。

如果按键是 **按键 → VDD**（按下=接 VCC），NONE 应该是 1023（强拉到顶），按下应该接近 0。**也不对**。

所以**真实电路很可能是**（这是 ADKEY 业界经典设计）：

```
                  PB5/ADC12
                     │
                  ┌──┴──┐
                  │  K  │  按键矩阵（按下=接 VDD 或接不同电阻到 GND）
                  └──┬──┘
        ┌────────────┼────────────┐
        │            │            │
       P/P         PREV         NEXT
      接 VCC       串 R2        串 R3
       (0Ω)       到 GND        到 GND
                  (12k)        (47k)

另外，从 PB5 出发有"另一条"上拉路径到 VDD：
  PB5 ──[R_pull_up]──► VDD
        ↑ 
  R_pull_up = GPIO 10kΩ 并联 ADC100kΩ = 9.09kΩ
```

> 但即便如此，理论 NONE 应该是 1023，实测却是 122。**关键推断：真实电路里 PB5 到 VDD 之间**还有**外接分压电阻**（比如按键矩阵是另一端接 GND 而不是 VDD），或者 GPIO 上拉实际并未生效。

#### 7.6.2 实测反推电路参数

让我们用**实测数据反推**——假设 ADKEY 电路是经典的"上拉到 VDD + 按键到 GND 串不同电阻"模型：

```
PB5 ──[R_up=9.09kΩ]──► VDD
  │
  ├─[R_key]──► GND   (按下)
  │
  └─ 开路              (抬起)
```

实测数据告诉我们：

| 状态 | R_key | 实测 ADC | 推算 V_PB5/Vref |
|---|---|---:|---:|
| 抬起 (NONE) | ∞ | 122 | 122/1023 ≈ 11.9% |
| PLAY (P/P) | 0 | 24 | 24/1023 ≈ 2.3% |
| PREV | 12k | 115 | 115/1023 ≈ 11.2% |
| NEXT | 47k | 120 | 120/1023 ≈ 11.7% |

**异常观察 1：PLAY=24 不应该是 0**——按下 P/P 后 PB5 应该是 0V（直接接地），ADC 应该是 0。但实测 = 24（24/1023 = 2.3% VDD）。这意味着：

- **PB5 不是直接接地**！按键和 PB5 之间还有电阻
- 或者 **GND 不是 0V**（板子 GND 有压降）
- 或者 **ADC 失调误差**（offset error）

**异常观察 2：PREV/NEXT 实测比例严重失调**——按 12k/47k 应该是 1:3.9 比例。但实测 115 vs 120 几乎是 1:1！

这说明 **R_up（PB5 到 VDD 的等效上拉）远远比 9.09kΩ 大得多**。让我反推：

对 PREV (12k 按下)：
```
V_PB5 / Vref = R_12k / (R_12k + R_up) = 115/1023 ≈ 11.24%
=> R_12k / (R_12k + R_up) = 0.1124
=> 12k / (12k + R_up) = 0.1124
=> 12k = 0.1124 × (12k + R_up)
=> 12k = 1.349k + 0.1124 × R_up
=> R_up = (12k - 1.349k) / 0.1124 ≈ 94.7kΩ
```

对 NEXT (47k 按下) 验证：
```
V_PB5 / Vref = 47k / (47k + 94.7k) = 47/141.7 ≈ 33.2%
=> 期望 ADC = 0.332 × 1023 ≈ 340
但实测 ADC = 120  (≈ 11.7%)
```

**严重不符**！意味着 12k 和 47k 也不是真实电阻值，或者电路根本不是这个模型。

#### 7.6.3 真实电路很可能是——ADKEY 不是按下=接地

更可能的解释：**板载 ADKEY 按下时不是把 PB5 短路到 GND，而是接 VDD 或接到不同电平**。我们看 [test_adkey.md §7.1](../docs/test_adkey.md) 的对照数据：

> | 上拉组合 | PLAY | PREV | NEXT | NONE |
> | GPIO 10kΩ + ADC12 100kΩ | 24 | 115 | 120 | 122/123 |
> | 仅 ADC12 100kΩ | 24 | 与 NONE 大量重叠在 32～34 | ... | 33 |
> | 仅 GPIO 10kΩ | 0 | 84 | 79～104 | 92 |

观察"仅 GPIO 10kΩ"那一行：**PLAY=0**！这说明按下 P/P 后 PB5 真的能拉到 0V——但只有当 GPIO 10kΩ 上拉**关闭**时才行。

合理解释：

1. **PLAY (P/P) 按下 = 把 PB5 短路到 GND**（0Ω 确实 = 0V）
2. **PREV / NEXT 按下 = 把 PB5 接到一个分压节点**（不是 GND，是某个中间电平）
3. **电路板上同时有两条上拉路径到 VDD**——一条是 GPIO 10kΩ（软件可控），另一条可能是**外接电阻**（板上固有的，无法关闭）

板子的真实电路极可能是：

```
       VDD ──[R_ext_top]──┬──[R_ext_bot]── GND
                          │
                          PB5 ──[GPIO 10k]── VDD
                                  [ADC 100k]
                          │
                          ├─ P/P ── GND (按下短路)
                          │
                          ├─ PREV ── 接 PB5 分压点 R12 (12k)
                          │
                          └─ NEXT ── 接 PB5 分压点 R47 (47k)
```

具体 R_ext_top / R_ext_bot / R12 / R47 的真实值需要**用万用表量 PB5 引脚在每个状态下的电压**才能确定。文档作者**未做这一步**——所以**理论值和实测值严重不符是正常的**，不要试图用文档里给出的理论值去校准任何东西。

#### 7.6.4 工程师的实际做法

面对"理论算不准"的现实，做 ADKEY 类电阻按键扫描的工程师通常这么做：

1. **不要算理论值**——直接看实测。四个按键各按 5 秒，记下 raw 值的 min/max/平均。
2. **取实测稳定值的中点作为阈值**：
   ```
   PLAY_max = (实测_PLAY_max + 实测_PREV_min) / 2 ≈ (24 + 115) / 2 ≈ 69
   PREV_max = (实测_PREV_max + 实测_NEXT_min) / 2 ≈ (115 + 120) / 2 ≈ 117
   ```
3. **在过渡区间留死区**——NEXT 与 NONE 只差 2 LSB，121 是不可信区间，映射为 UNKNOWN。
4. **温度/电源变化会偏移实测值**——阶段二 §7 的双上拉对照就是为了在多种工况下保持稳定。

#### 7.6.5 给学习者的关键启示

> **理论值是教学工具，实测值才是工程依据**。

- 任何电阻分压的公式都可以算，但板子上的真实电阻值受 PCB 走线、芯片内部电阻、温度、电源精度影响，**理论值与实测值偏差 10%~50% 是常态**。
- ADC 读数**只反映相对关系**（NONE > NEXT > PREV > PLAY），**不反映绝对电压**——除非你专门校准过 Vref 和失调误差。
- 如果你需要**精确电压读数**（电池电量、温度计），必须：
  1. 用万用表实测电源电压，把 3.3V 替换成实测值
  2. 用 [test_adkey.md §1.2](../docs/test_adkey.md) 的对照实验找出哪些上拉真正生效
  3. 用两点校准（raw=0 对应 0V，raw=1023 对应 VDD）替换理论 LSB 公式

**回到本测试**：我们用 AT24C02——不，我们用 ADKEY——**目的是区分 4 种按键状态**，不是测量绝对电压。所以**实测阈值表**（§7.3）**就是工程答案**，理论公式只是用来辅助理解为什么 NONE > NEXT > PREV > PLAY。

---

## 8. 进阶：消抖 / 长按 / 连发

### 8.1 为什么需要状态机？

**问题 1：机械按键抖动**
- 物理触点金属片按下瞬间会**反弹** 1~5 ms
- ADC 在这段时间可能读到 PLAY(24)、NONE(122)、PLAY(24) 交替
- 直接打印会得到 `PLAY → NONE → PLAY → ...` 一堆噪声消息

**问题 2：消费电子要求**
- 单击：按一次发一条 SHORT
- 长按：700ms 后发 LONG（音量加减、自动开关机等场景）
- 连发（HOLD）：长按之后每 200ms 重复（音量持续加减）
- 双击、组合键……

### 8.2 阶段三：5ms × 5 消抖

**思路**：每 5ms 采一次，连续 5 次相同才认为稳定。

```
5ms × 5 = 25ms 消抖窗
```

为什么是 5ms 而不是 1ms？
- 抖动最长 5ms，5ms 采样能稳定错过抖动尾
- 5ms 也足够灵敏（人手反应 100~200ms）

为什么是 5 次而不是 3 次？
- 3 次 15ms 太短，机械按键抖动可能漏
- 5 次 25ms 是消费电子典型值

**状态机**（test_adkey.c L250-308）：

```
                ┌─────────────────────────────────┐
                │                                 │
                ▼                                 │
        ┌─────────────┐  不一致   ┌─────────────┐  │
   ───► │ candidate=NONE│ ───────► │  新候选键    │  │
        │ same_count=0 │           │ same_count=1│  │
        └──────┬──────┘           └──────┬──────┘  │
               │ 一致                    │ 一致    │
               ▼                         ▼         │
        ┌─────────────┐           ┌─────────────┐  │
        │  计数+1     │ ─────────►│  计数 = 5   │ ─┘
        │ (max=5)     │           │  → 稳定!    │
        └─────────────┘           └──────┬──────┘
                                          │ 与 stable 不同
                                          ▼
                                    发边沿消息:
                                    SHORT ↑ / SHORT_UP ↑
```

**核心规则**：

| 当前采样 | 候选键 | 计数 | 稳定键 | 动作 |
|---|---|---|---|---|
| NONE | NONE | 0 | NONE | 无 |
| PLAY | NONE → PLAY | 1 | NONE | 等 |
| PLAY | PLAY | 2~4 | NONE | 等 |
| PLAY | PLAY | **5** | NONE | 发 `SHORT PLAY`，stable = PLAY |
| PLAY | PLAY | 5 | PLAY | 无（保持期间不再发） |
| NONE | NONE | 5 | PLAY | 发 `SHORT_UP PLAY`，stable = NONE |

### 8.3 阶段四：700ms 长按

在阶段三基础上加一个计时器：

```
SHORT 之后 ≥ 700ms 还在按？ → 发 LONG
```

**关键代码**（test_adkey.c L407-419）：

```c
if ((stable_key != KEY_NONE) &&
    (sampled_key == stable_key) &&
    !long_sent &&
    ((u32)(current_tick - press_tick) >= ADKEY_LONG_TICKS)) {  // 140 ticks × 5ms = 700ms
    u16 long_message = (u16)(KEY_LONG | stable_key);
    long_sent = true;
    long_tick = current_tick;
    TEST_LOG("msg=0x%04x KEY_LONG %s ...", long_message, ...);
}
```

**消息分类**（test_adkey.h）：

| 消息 | 值 | 触发 |
|---|---:|---|
| `KEY_SHORT` | `0x0000 \| key` | 稳定按下 25ms |
| `KEY_SHORT_UP` | `0x0800 \| key` | 700ms 前稳定抬起 |
| `KEY_LONG` | `0x0a00 \| key` | SHORT 之后 700ms |
| `KEY_LONG_UP` | `0x0c00 \| key` | 700ms 之后稳定抬起 |
| `KEY_HOLD` | `0x0e00 \| key` | LONG 之后每 200ms |

> **设计哲学**：用 `msg` 的高 8 位区分事件类型，低 8 位是 key code。一个 `u16` 装下所有按键事件。

### 8.4 阶段五：200ms HOLD 连发

LONG 之后每 200ms 重发 HOLD，松开立刻发 LONG_UP 并停止 HOLD。

```c
if (long_sent && ((u32)(current_tick - long_tick) >= ADKEY_HOLD_TICKS)) {
    // 40 ticks × 5ms = 200ms
    long_tick = current_tick;
    TEST_LOG("msg=0x%04x KEY_HOLD ...", ...);
}
```

**典型场景**：

```
按 1.6 秒：
  t=0    SHORT_PLAY    (稳定 25ms)
  t=700  LONG_PLAY     (达到长按阈值)
  t=900  HOLD_PLAY     (LONG 后 200ms)
  t=1100 HOLD_PLAY     (再 200ms)
  t=1300 HOLD_PLAY
  t=1500 HOLD_PLAY
  t=1600 LONG_UP_PLAY  (松开)
```

### 8.5 时基选择

| 计时器 | 用途 | 配置 |
|---|---|---|
| **TMR0** | 1ms 系统 tick（main.c） | 全局 |
| **TMR1** | 5ms 扫描（test_adkey_debounce） | `TMR1PR = 5000-1`，`INCSEL=1MHz` |
| **TMR2** | 1µs 自由运行（delay_us/delay_ms） | `PR = 0xFFFFFFFF` |

> TMR1 选择 5ms 不是 1ms：因为 ISR 越频繁越耗 CPU，而按键状态机 5ms 完全够用。

---

## 9. 调试与排错

### 9.1 常见症状速查表

| 症状 | 原因 | 修复 |
|---|---|---|
| **一直读 0** | 引脚被强制拉低（如按键卡死接地） | 检查硬件 |
| **一直读 1023 / 最大值** | 上拉过强 + Vin = VDD；或 ADCAEN/ADCANGIO 没开 | 检查 §5.1 第 5 步 |
| **读数偏低 20~30%** | VDD 实测不到 3.3V 或参考电压不准 | 用万用表量 VDD |
| **读数抖动 ±50 LSB** | 信号源阻抗太高 / 建立时间不够 | 改 `SADCST` 加建立时间 |
| **持续打印 `[TIMEOUT]`** | 时钟门没开 / ADCEN 没设 / SADCCH 没重写 | 检查 §5.1 第 1、5 步 + §6.1 第 A 步 |
| **NEXT 和 NONE 偶尔混淆** | 死区不够宽 | 拉大 NONE_MIN 与 NEXT_MAX 间距 |
| **按住 P/P 超过 10 秒芯片复位** | PB5 兼 10S Reset 主唤醒源 | 长按测试别超过 8 秒 |

### 9.2 验证清单（自查）

按顺序检查每一项：

- [ ] `CLKCON0[28] = 1`（6MHz 时钟源）
- [ ] `CLKGAT0[13] = 1`（SARADC 时钟门）
- [ ] `SADCCON` 包含 `ADCAEN | ADCANGIO | ADCEN | CH12PUEN`
- [ ] `SADCBAUD` 在 0~9 范围（保证 ≤ 1 MHz 位时钟）
- [ ] 引脚 `DIR=1`（输入）、`DE=1`、`FEN=0`（不映射外设）
- [ ] 上拉电阻已使能（GPIO + ADC 各一路）
- [ ] 每次读 ADC 前**重新写** `SADCCH = BIT(n)`
- [ ] 读 `SADCDATn` 时 `& 0x3FF` mask

### 9.3 用示波器/逻辑分析仪辅助

SARADC 内部时序看不到，但可以观察：
- **引脚电压**：用万用表量 PB5，按键按下应该能看到电压从 122 跳到 24
- **电源**：VDD 应该在 3.0~3.6V 范围
- **时钟门**：如果 `CLKGAT0[13]=0`，SARADC 完全不动

### 9.4 多通道扫描模式（扩展）

手册 §11.3 步骤 4 说"可同时使能多个通道"：

```c
SADCCH = BIT(0) | BIT(3) | BIT(12);  // 同时启动 ADC0/ADC3/ADC12
```

但**同一时刻只转换 1 个**！它们是**串行**完成的（先 0，再 3，再 12）。ADCPND 仍然是最后完成的那个置位。

> **本测试用单通道**（每次只写 BIT(12)），逻辑最简单。

---

## 10. 练习题

### 10.1 入门：读一个电位器

**硬件**：PB5 接一个电位器（3 端：VCC、GND、PB5）。旋转时 PB5 电压 0~3.3V 连续变化。

**目标**：每 100ms 读一次 ADC，打印 0~1023 原始值。

**提示**：电位器按下=接地，抬起=VCC，没有中间值。所以本测试**不要按键阈值**，直接打印原始值即可。

### 10.2 进阶：温度传感器

**硬件**：把 PB5 改接到 LM35 温度传感器（输出 10mV/℃）。

**目标**：把原始值换算成摄氏度，打印 `"T = 25.3 C"`。

**提示**：
```
LM35:  10 mV/°C → 0°C = 0 V, 25°C = 250 mV
ADC:   0~1023 = 0~3.3V → 1 LSB = 3.22 mV
T = raw × 3.22mV / 10mV = raw × 0.322 °C
```

### 10.3 综合：电池电量计

**硬件**：把 PB5 通过分压电阻（100k + 100k）接到 VBAT。

**目标**：显示电池百分比（4.2V=100%, 3.3V=0%）。

**提示**：
```
VBAT = 2 × ADC × 3.22mV (因为分压)
4.2V → 1305 LSB → 显示 100%
3.3V → 1026 LSB → 显示 0%
```

### 10.4 高难度：滑动平均滤波

**问题**：原始 ADC 读数偶尔跳 ±5 LSB。

**目标**：维护一个长度 8 的环形 buffer，每次返回 8 个样本的平均值。

**提示**：

```c
#define N  8
static u32  ring[N] = {0};
static u32  sum = 0;
static u32  idx = 0;

u32 adc_filtered(u32 new_sample) {
    sum -= ring[idx];
    ring[idx] = new_sample;
    sum += new_sample;
    idx = (idx + 1) % N;
    return sum / N;
}
```

---

## 11. 与其它外设的对比

| 外设 | 输入类型 | 输出 | 本测试核心 API |
|---|---|---|---|
| **ADC** | 模拟电压 | 10-bit 数字 | `SADCCON` / `SADCCH` / `SADCDATn` |
| GPIO | 数字电平 | 输入/输出 | `GPIOnDIR` / `GPIOnSET` / `GPIOn` |
| UART | 串行 bit 流 | 字节流 | `UARTnDATA` / `UARTnCON` |
| I2C | SCL/SDA 时序 | 字节 + ACK | `IICCON0` / `IICDATA` |
| SPI | CLK/MOSI/MISO/CS | 字节 | `SPI1CON` / `SPI1BUF` |
| Timer | 内部时钟 | 中断/计数 | `TMRnCNT` / `TMRnPR` |

**ADC 最特殊**：是 BT892X **唯一一个输入是连续模拟信号的控制器**。其它都是数字信号（0/1）。

---

## 12. 速查清单（Cheat Sheet）

```c
// ===== 初始化顺序（按手册 §11.3）=====
CLKCON0 |= BIT(28);           // 1. 时钟源选 6MHz
CLKGAT0 |= BIT(13);           // 2. 开 SARADC 时钟门
GPIOBPU |= BIT(5);            // 3. PB5 GPIO 10kΩ 上拉
SADCBAUD = 5;                 // 4. 6MHz / (2×6) = 500 kHz
SADCCON = BIT(19)|BIT(18)|BIT(16)|BIT(12);  // 5. ADCAEN|ADCANGIO|ADCEN|CH12PUEN

// ===== 单次采样 =====
SADCCH = BIT(12);             // 启动 ADC12 转换 + 清 ADCPND
while (!(SADCCH & BIT(16)));  // 等 ADCPND = 1
u32 raw = SADCDAT12 & 0x3FF;  // 读 10-bit 结果

// ===== 阈值映射（AT24C02 实测值示例，不是真的 AT24C02，这是 ADKEY）=====
if      (raw <= 69)   key = KEY_PLAY;
else if (raw <= 117)  key = KEY_PREV;
else if (raw <= 120)  key = KEY_NEXT;
else if (raw >= 122)  key = KEY_NONE;
else                  key = KEY_UNKNOWN;  // 死区 121
```

---

## 13. 一图流总结

```
                  ┌──────────────────────┐
                  │  Vin (PB5/ADC12)     │
                  │  按键分压 0~3.3V     │
                  └──────────┬───────────┘
                             │
                ┌────────────┴────────────┐
                │  Sample & Hold (S&H)    │  采 1 次 = 充 1 次电容
                │  (内部电容 + 开关)      │
                └────────────┬────────────┘
                             │ V_hold (稳定电压)
                             ▼
        ┌────────────────────────────────────────┐
        │  SAR 逐次逼近（10 个时钟周期）          │
        │  - 二分搜索 bit 9 → bit 8 → ... → bit 0│
        │  - 每步: DAC 猜值 → 比较器比较 → 留/弃  │
        └────────────────────┬───────────────────┘
                             │ 10-bit 数字
                             ▼
                  ┌──────────────────────┐
                  │  SADCDAT12[9:0]      │  = 0 ~ 1023
                  │  & 0x3FF 拿到结果    │
                  └──────────┬───────────┘
                             │
                             ▼
                  阈值映射 → PLAY/PREV/NEXT/NONE
                             │
                             ▼
                  消抖状态机 (5ms × 5)
                             │
                             ▼
                  边沿消息 SHORT / SHORT_UP
                             │
                             ▼
                  长按检测 (700ms)
                             │
                             ▼
                  HOLD 连发 (200ms)
```

---

## 14. 相关文档

- [docs/periph_adkey.md](periph_adkey.md)（如未提供，可参考 [test_adkey.md](test_adkey.md) §1）
- [docs/test_adkey.md](test_adkey.md)（完整 5 阶段测试报告）
- [docs/SARADC_CTL（逐次逼近型 ADC）数据手册摘要.md](SARADC_CTL%20%28逐次逼近型%20ADC%29%20数据手册摘要.md)（手册精要）
- [bt892x_pinfunction.md §4.2](../bt892x_pinfunction.md)（PB5/ADC12 引脚定义）
- [BT892X_UserManual_Driver.md §11](../BT892X_UserManual_Driver.md)（SARADC 完整寄存器手册）

---

## 15. 版本

- 2026-07-23 v1：从零开始学 ADC，结合 BT892X ADKEY 测试示例