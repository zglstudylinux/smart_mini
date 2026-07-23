# BT892X 硬件 I2C 测试报告

> **测试日期**：2026-07-17
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §8 IIC](../BT892X_UserManual_Driver.md)
> - [bt892x_pinfunction.md §4.3 / §8.5](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**硬件 IIC 控制器** + AT24C02 EEPROM + 逻辑分析仪时序验证

---

## 0. 文档结构

本测试使用 BT892X 内置的**硬件 IIC 控制器**，所有时序由 IIC 寄存器（IICCON0/1/CMDA/DATA）配置。

| 程序 | 路径 | 用途 | 验证方式 |
|---|---|---|---|
| **test_i2c.c** | `smart_mini/test/test_i2c.c` | AT24C02 功能测试（5 个子测试） | 串口打印 ACK/数据 |
| **test_i2c_la.c** | `smart_mini/test/test_i2c_la.c` | 逻辑分析仪时序验证 | LA 测量 SCL 周期 |

> **与软件 bit-bang I2C 对比，请看 [test_soft_i2c.md](test_soft_i2c.md)**

---

## 1. 手册源码引用

每一条寄存器配置都标注了手册出处。

### 1.1 IICON0（手册 [§8.2](../BT892X_UserManual_Driver.md) 第 554-568 行）

```
Bit | Name      | Mode | Default | Description
----|-----------|------|---------|------------------------------------------
 31 | DONE      | R    | 0       | IIC 传输完成标志
 30 | ACKSTATUS | R    | 0       | 0=ACK，1=NAK
 29 | CLR_DONE  | W    | 0       | 写 1 清除 DONE
 28 | KS        | W    | 0       | 写 1 启动传输（Kick Start）
 27 | CLR_ALL   | W    | 0       | 写 1 清除所有状态
9:4 | POSDIV    | WR   | 0       | SCL 高电平分频。值 N 表示分频 (N+1)
3:2 | HOLDCNT   | WR   | 0       | SCL 下降沿后 SDA 保持周期。0=1周期，1=2周期
  1 | INTEN     | WR   | 0       | IIC 中断使能
  0 | IIC_EN    | WR   | 0       | IIC 主控使能

波特率公式: IICCLK = source_clk / (preclkdiv + 1)，SCL = IICCLK / (posdiv + 1)
```

### 1.2 IICON1（手册 [§8.2](../BT892X_UserManual_Driver.md) 第 570-584 行）

```
Bit | Name       | Description
----|------------|-------------------------------------------
 12 | TXNAK_EN   | 读最后一字节时发 NAK（手册 §8.2）
 11 | STOP_EN    | 发 STOP（手册 §8.2）
 10 | WDAT_EN    | 发数据（手册 §8.2）
  9 | RDAT_EN    | 接收数据（手册 §8.2）
  8 | CTL1_EN    | 发控制字节 1（重复起始第二地址，手册 §8.2）
  7 | START1_EN  | 发重复起始 Sr（手册 §8.2）
  6 | ADR1_EN    | 发 ADR1（手册 §8.2）
  5 | ADR0_EN    | 发 ADR0（手册 §8.2）
  4 | CTL0_EN    | 发 CTL0（地址+R/W，手册 §8.2）
  3 | START0_EN  | 发起始 S（手册 §8.2）
2:0 | DATA_CNT   | 收发数据字节数（0~N，手册 §8.2）
```

### 1.3 IICCMDA（手册 [§8.2](../BT892X_UserManual_Driver.md) 第 586-593 行）

```
Bit   | Name | Description
------|------|-------------------------------------------
31:24 | CTL1 | 控制字节 1（重复起始第二地址，手册 §8.2）
23:16 | ADR1 | 地址 1（备用子地址，手册 §8.2）
15:8  | ADR0 | 地址 0（子地址字节 1，手册 §8.2）
 7:0  | CTL0 | 控制字节 0（地址 + R/W，手册 §8.2）
```

### 1.4 IICDATA（手册 [§8.2](../BT892X_UserManual_Driver.md) 第 595-602 行）

```
Bit   | Name | Description
------|------|-------------------------------------------
31:24 | DATA3 | 数据 3（手册 §8.2）
23:16 | DATA2 | 数据 2（手册 §8.2）
15:8  | DATA1 | 数据 1（手册 §8.2）
 7:0  | DATA0 | 数据 0（最先发/收，手册 §8.2）
```

### 1.5 使用指南（手册 [§8.3](../BT892X_UserManual_Driver.md) 第 604-614 行）

> 1. 配置 IO 映射，SDA 设置上拉使能
> 2. 选择时钟源（RC2M 或 XOSC26M），设置预分频
> 3. 配置 IICON0
> 4. 配置 IICCMDA（控制字节和地址字节）
> 5. 配置 IICDATA（写入数据）
> 6. 配置 IICON1
> 7. 写 KS 启动
> 8. 等待 DONE 标志或中断
> 9. 清除 DONE 标志，更新 IICCMDA 或 IICDATA
> 10. 循环步骤 7

### 1.6 时钟源（手册 [§8.1](../BT892X_UserManual_Driver.md) 第 546-550 行）

> 支持异步时钟源（**RC2M** 或 **XOSC26M**）

手册 §8.2 公式：

```
SCL = source_clk / ((preclkdiv + 1) * (posdiv + 1))
```

> **注**：手册没有给出 preclkdiv 寄存器的具体位置或默认值，本测试使用其默认值。

### 1.7 时钟门控（来自用户提供的 CLKGAT 寄存器定义表）

```
CLKGAT2[0] = IIC
```

> 本工程未在 BT892X_UserManual_Driver.md 中找到 CLKGAT 寄存器的逐位定义表。该表由用户提供（图片形式），作为本测试时钟门控配置的来源。

### 1.8 引脚定义（[bt892x_pinfunction.md](../bt892x_pinfunction.md)）

**第 8.5 节 IIC 信号**（第 233-239 行）：

| 信号 | 可分配 PAD |
|---|---|
| IIC_CLK (SCL) | PA6, PB1, **PE6**, PF4 |
| IIC_DAT (SDA) | PA5, PA7, PB0, PB2, PB3, PB4, PE5, **PE7**, PF5 |

**第 4.3 节 PE6/PE7 行**（第 128-129 行）：

- PE6 行 IIC 列：**IIC_CLK-G5/G6**
- PE7 行 IIC 列：**IIC_DAT-G5**

> **G5 是同时承载 PE6 SCL 和 PE7 SDA 的唯一 Group**。

### 1.9 IIC 引脚映射寄存器（[bt892x_pinfunction.md](../bt892x_pinfunction.md) 第 86-92 行）

> `FUNCMCON2[24:27]` = IIC Group（映射控制位）
> `FUNCMCON2[31:28]` = DVP Group（其他）

### 1.10 GPIO PAD 寄存器（[header/sfr.h](../../smart_mini/header/sfr.h) 第 449-462 行）

每个 GPIO 端口有 DE（数字使能）、FEN（外设功能映射）、PU（上拉）、PD（下拉）、DIR（方向）等寄存器。硬件 IIC 控制器要接管引脚，必须开 `FEN`。

---

## 2. 测试程序 1：test_i2c.c（AT24C02 功能验证）

### 2.1 引脚分配

| 引脚 | 角色 | 手册依据 |
|---|---|---|
| PE6 | SCL (IIC_CLK-G5) | [pinfunction §4.3](../bt892x_pinfunction.md) |
| PE7 | SDA (IIC_DAT-G5) | [pinfunction §4.3](../bt892x_pinfunction.md) |

### 2.2 寄存器配置原理

每行都注明来源：

```c
// ============== test_i2c_init() ==============
// §8.3 step 1：开 IIC 时钟门（CLKGAT2[0] = IIC，用户提供的 CLKGAT 表）
CLKGAT2 |= BIT(0);

// §8.3 step 1 续：PE6/PE7 PAD 配置
GPIOEDE   |=  PE6_7_MASK;   // 数字 IO 使能（手册 §3.2）
GPIOEFEN  |=  PE6_7_MASK;   // 外设功能映射 — 让 IIC 接管（手册 §3.2）
GPIOEPU   |=  PE6_7_MASK;   // 10K 上拉 — §8.3 要求 SDA 上拉
GPIOEPD   &= ~PE6_7_MASK;
GPIOEDIR  &= ~PE6_7_MASK;

// §8.3 step 1 + §3.3：FUNCMCON2[24:27] = IIC Group G5
// （手册 §3.3 + pinfunction G5 = PE6 SCL + PE7 SDA）
FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

// §8.3 step 2/3：IICON0 主体配置（手册 §8.2 IICCON0 表）
IICCON0 = (0u  << 2)    // HOLDCNT = 0（手册表）
        | (19u << 4)    // POSDIV = 19（÷20）
        | IIC_EN;       // IIC_EN = 1（手册表）
IICCON0 |= IIC_CLR_ALL;    // 手册 bit 27
delay_us(100);
```

### 2.3 POSDIV 选择依据

手册 §8.2 公式：`SCL = source_clk / ((preclkdiv + 1) × (posdiv + 1))`

**假设**：手册 §8.1 列出 source_clk 为 RC2M 或 XOSC26M。本测试**假设 source_clk = RC2M**，并使用手册建议的 `posdiv=19 /20` 以获得 100 kHz SCL（实际值由 LA 测试 [§3](#3-测试程序-2test_i2c_lac-逻辑分析仪时序验证) 验证）。

### 2.4 数据操作流程（按手册 §8.3）

**写事务（start_iic_write）**：

```c
// 1. IICCON0 |= CLR_ALL          (§8.2 bit 27)
// 2. IICCMDA = (addr<<1)|R/W + reg<<8   (§8.2 table: CTL0[7:0], ADR0[15:8])
// 3. IICDATA = data              (§8.2 table: DATA0[7:0]...DATA3[31:24])
// 4. IICCON1 = START0|CTL0|ADR0|WDAT|STOP|DATA_CNT  (§8.2 table: bits 3,4,5,10,11,2:0)
// 5. IICCON0 |= KS               (§8.2 bit 28)
// 6. wait DONE (poll IICCON0 bit 31)
// 7. read ACKSTATUS bit 30
// 8. IICCON0 |= CLR_DONE          (§8.2 bit 29)
```

**读事务（start_iic_read）**：用重复起始（START1 + CTL1），参考手册 §8.3 step 6 描述。

### 2.5 测试流程

| 子测试 | 验证 | 期望 |
|---|---|---|
| **Test 1** | START + 0xA0 + STOP 探测 | ACK |
| **Test 2** | 扫描 0x08~0x77 | AT24C02 @ 0x50 ACK |
| **Test 3** | 写 AT24C02[0x00] = 0x55 | ACK |
| **Test 4** | 读 AT24C02[0x00] | 读到 0x55 → WRITE-READ PASS |
| **Test 5** | 写读 4 字节 pattern (0xDE 0xAD 0xBE 0xEF) | 全部匹配 → PATTERN PASS |

### 2.6 实测结果

实测输出（用户验证）：

```
[TEST] [Test 1] Single address probe @ 0x50
[TEST]   Probe 0x50 result: ACK
[TEST] [Test 2] Address scan 0x08..0x77
[TEST]   Found device at 0x50
[TEST]   Scan done: 1 device(s) found
[TEST] [Test 3] Write 0x55 to AT24C02 reg 0x00
[TEST]   Write: ACK
[TEST] [Test 4] Read AT24C02 reg 0x00 (expect 0x55)
[TEST]   Read: ACK, data=0x55
[TEST]   WRITE-READ PASS
[TEST] [Test 5] Write-read 4 bytes pattern @ reg 0x10
[TEST]   Write 0xDE 0xAD 0xBE 0xEF: ACK
[TEST]   Read: ACK, data=0xDE 0xAD 0xBE 0xEF
[TEST]   PATTERN PASS
```

**结论**：✅ 全部 5 个子测试通过，BT892X 硬件 IIC 与 AT24C02 通信功能正常。

---

## 3. 测试程序 2：test_i2c_la.c（逻辑分析仪时序验证）

### 3.1 目的

当 PE6/PE7 接逻辑分析仪（无法同时接 AT24C02）时跑此测试，让 IIC 控制器连续发出 START+ADDR+STOP 事务（不需要 ACK 应答），用 LA 测量实际 SCL 周期。

**用途**：
- 验证 §8.2 公式中的 source_clk 实际值（手册说 RC2M 或 XOSC26M 但没给具体频率）
- 验证 POSDIV 19 /20 的实际效果
- 提供软件测量的 ground truth

### 3.2 硬件连接

```
LA:  CH1 → PE6 (SCL)
     CH2 → PE7 (SDA)
     GND → GND

AT24C02: 断开 VCC 或整个模块（不影响 SCL 驱动）
```

### 3.3 寄存器配置

与 test_i2c.c 的 `test_i2c_init()` 相同（手册依据相同）：

```c
// §8.3 step 1: 开 IIC 时钟门（CLKGAT2[0] = IIC，详见 CLKGAT 表）
CLKGAT2 |= BIT(0);
// §3.2: 数字 IO
GPIOEDE  |= PE6_7_MASK;
// §3.2: 外设功能映射
GPIOEFEN |= PE6_7_MASK;
// §8.3: SDA 上拉
GPIOEPU  |= PE6_7_MASK;
// §3.3: FUNCMCON2[24:27] = IIC Group G5
FUNCMCON2 |= (0x5u << 24);
// §8.2 表: POSDIV=19, IIC_EN=1
IICCON0 = (19u << 4) | IIC_EN;
IICCON0 |= IIC_CLR_ALL;
```

### 3.4 测试流程

1. 启动 → 串口打印初始寄存器状态
2. Burst 1：50 次事务（START + 0xA0 + STOP），等待 LA 触发
3. 5 秒间隔
4. Burst 2：50 次事务（用户可验证重复性）
5. 串口提示用户在 LA 上测量

### 3.5 LA 测量步骤

1. 在 LA 上设置 CH1（PE6）触发为**下降沿**
2. 测量从一个 SCL 下降沿到下一个下降沿的距离
3. 这就是 **SCL 周期 T_scl**
4. 实际 IICK 频率 = 1 / T_scl / (POSDIV + 1) = 1 / T_scl / 20

### 3.6 实测结果

LA 实测数据（用户验证）：

```
SCL 单个完整周期 ≈ 7.94 µs  (POSDIV=19)
→ 实际 IICK ≈ 1/7.94µs × 20 ≈ 2.52 MHz
```

> **注**：手册 §8.1 给出 source 可选 RC2M 或 XOSC26M 但没给具体频率。本工程实测 IICK ≈ 2.5 MHz，但 BT892X datasheet 没有给出 RC2M 的精确频率标定值，无法推断具体路径。

### 3.7 失败排查

| LA 现象 | 原因 | 解决 |
|---|---|---|
| 完全无波形 | IIC 时钟门未开 | 确认 `CLKGAT2 \|= BIT(0)` |
| SCL 一直是高 | 没启动事务 | LA 触发沿需在 burst 时间窗内 |
| SCL 一直是低 | 状态卡死，电源复位后重试 | 单次烧录断电重启 |
| 频率明显偏差 | POSDIV 写错 | 检查 IICCON0[9:4] = 19 |

---

## 4. 完整寄存器表（来自手册 §8.2 + 用户提供的 CLKGAT 表）

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `CLKGAT2` | 0x3E4 (`SFR0_BASE+0x3E*4`) | `\|= BIT(0)` 开 IIC 时钟门 | 用户提供的 CLKGAT 表 |
| `FUNCMCON2` | 0x024 (`SFR0_BASE+0x9*4`) | `[24:27] = 0x5` (G5) | §3.3, pinfunction §4.3 |
| `GPIOEDE` | 0x690 (`SFR6_BASE+0x4*4`) | `\|= PE6_7_MASK` 数字使能 | §3.2 |
| `GPIOEFEN` | 0x694 (`SFR6_BASE+0x5*4`) | `\|= PE6_7_MASK` 外设功能映射 | §3.2 |
| `GPIOEPU` | 0x69C (`SFR6_BASE+0xD*4`) | `\|= PE6_7_MASK` 10K 上拉 | §8.3 step 1 |
| `GPIOEPD` | 0x6A0 (`SFR6_BASE+0x10*4`) | `&= ~PE6_7_MASK` 关闭下拉 | §3.2 |
| `GPIOEDIR` | 0x68C (`SFR6_BASE+0x3*4`) | `&= ~PE6_7_MASK` 输出 | §3.2 |
| `IICCON0` | 0x51C (`SFR5_BASE+0x7*4`) | `(19<<4) \| IIC_EN` POSDIV=19 + 使能 | §8.2 IICCON0 表 |
| `IICCON0` | 0x51C | `\|= IIC_KS` 启动 | §8.2 bit 28 |
| `IICCON0` | 0x51C | `\|= IIC_CLR_ALL` 清状态 | §8.2 bit 27 |
| `IICCON0` | 0x51C | `\|= IIC_CLR_DONE` 清完成 | §8.2 bit 29 |
| `IICCON0` | 0x51C | bit 30 = ACKSTATUS | §8.2 bit 30 |
| `IICCON0` | 0x51C | bit 31 = DONE | §8.2 bit 31 |
| `IICCON1` | 0x520 (`SFR5_BASE+0x8*4`) | `START\|CTL\|ADR\|WDAT\|STOP\|RXNAK\|DATA_CNT` | §8.2 IICON1 表 |
| `IICCMDA` | 0x524 (`SFR5_BASE+0x9*4`) | `[7:0]=CTL0`, `[15:8]=ADR0` | §8.2 IICCMDA 表 |
| `IICDATA` | 0x528 (`SFR5_BASE+0xA*4`) | `[7:0]=DATA0` | §8.2 IICDATA 表 |

---

## 5. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| 编译错误 `undeclared GPIOxxx` | `header/sfr.h` 没包含 | 检查 `test_common.h`（已含 sfr.h） |
| Test 1 ACK 持续 NAK | IIC 时钟门未开 | 确认 `CLKGAT2 \|= BIT(0)` |
| Test 1 稳定 NAK | PAD 配置错（G5 没启）| 确认 `FUNCMCON2 = ... \| 0x5<<24` + `GPIOEFEN \|=` |
| 全部地址 NAK | AT24C02 没接好/没上拉 | 检查 4 个接线（VCC/GND/SDA/SCL/A0/A1/A2） |
| 写后再读不一致 | 写周期未结束 + 没加 delay_ms(10) | 加 5~10ms 等待 |
| 波形不错但 ACK 失败 | 上拉电阻问题 | 万用表量 SDA idle 约 VCC 或 4.7K |

---

## 6. 工程意义

- **测试文件**：保留 `test_i2c.c`（AT24C02 功能）和 `test_i2c_la.c`（LA 时序）—— 两个互补测试
- **与软件 bit-bang 对比**：见 [test_soft_i2c.md](test_soft_i2c.md) 的手动 GPIO 模拟 I2C
- **手册依据**：每一条寄存器配置都列出对应的 [BT892X_UserManual_Driver.md](../BT892X_UserManual_Driver.md) 章节或 [bt892x_pinfunction.md](../bt892x_pinfunction.md) 表

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_i2c.c` | AT24C02 功能测试（硬件 IIC） |
| `smart_mini/test/test_i2c.h` | 头文件 |
| `smart_mini/test/test_i2c_la.c` | 逻辑分析仪时序测试（硬件 IIC） |
| `smart_mini/test/test_i2c_la.h` | 头文件 |
| `smart_mini/test/test_common.h` | 共用 `TEST_LOG` 宏 |
| `smart_mini/main.c` | 入口（按宏开关调用对应测试） |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 寄存器宏定义（第 359-362 行 IIC 寄存器） |
| `docs/BT892X_UserManual_Driver.md` | 芯片寄存器手册（§8 IIC，第 544+ 行） |
| `docs/bt892x_pinfunction.md` | 引脚功能定义（§4.3 PE6/PE7、§8.5 IIC 信号、第 86-92 行 Group 控制位） |