# BT892X 软件 I2C（GPIO bit-bang）测试报告

> **测试日期**：2026-07-17
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §8.1（IIC 特性）](../BT892X_UserManual_Driver.md)
> - [bt892x_pinfunction.md §4.3（PE6/PE7 GPIO）](../bt892x_pinfunction.md)
> - [header/sfr.h GPIO PE 寄存器](../../smart_mini/header/sfr.h)（第 450-462 行）
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**软件 bit-bang I2C（手动 GPIO）** + AT24C02 EEPROM + 逻辑分析仪时序验证

---

## 0. 文档结构

本测试**不使用硬件 IIC 控制器**，I2C 协议完全由软件在 PE6/PE7 上 GPIO bit-bang 实现。适用场景：
- 硬件 IIC 控制器被占用或不可用
- 测试 GPIO 时序特性
- 不依赖专用硬件验证 I2C 协议

| 程序 | 路径 | 用途 | 验证方式 |
|---|---|---|---|
| **test_i2c_gpio.c** | `smart_mini/test/test_i2c_gpio.c` | AT24C02 功能测试（5 个子测试） | 串口打印 ACK/数据 |
| **test_i2c_gpio_la.c** | `smart_mini/test/test_i2c_gpio_la.c` | 逻辑分析仪时序验证 | LA 测量 SCL 周期 |

> **与硬件 I2C 对比，请看 [test_hard_i2c.md](test_hard_i2c.md)**

---

## 1. I2C 协议参考（通用规范）

以下为 I2C 总线规范（非芯片特定）：

- **SCL**：主机控制的时钟线，始终由主机驱动
- **SDA**：双向数据线，主从两侧均为开漏输出
- **START 条件**：SCL 为高时 SDA 下降
- **STOP 条件**：SCL 为高时 SDA 上升
- **数据有效**：SCL 高电平期间 SDA 必须稳定（数据仅在 SCL 低时变化）
- **ACK 位**：每字节后第 9 个 SCL 时钟，从机将 SDA 拉低（0）表示应答
- **NACK**：从机在第 9 个 SCL 时钟保持 SDA 高（1）表示非应答
- **标准模式时序**：T_low ≥ 4.7 µs，T_high ≥ 4.0 µs，T_su_dat ≥ 250 ns
- **字节序**：MSB 先发（bit 7 先发送）

---

## 2. 手册源码引用

本测试**不使用硬件 IIC 控制器**，仅使用 GPIO 寄存器（[Sec.3.2 GPIO 控制](../BT892X_UserManual_Driver.md) 和 [sfr.h](../../smart_mini/header/sfr.h) 第 450-462 行）。

### 2.1 GPIO PE 端口寄存器（[header/sfr.h](../../smart_mini/header/sfr.h) 第 450-462 行）

| 寄存器 | 地址 | 描述 |
|---|---|---|
| `GPIOEDE` | 0x690 | 数字使能（1=使能） |
| `GPIOEFEN` | 0x694 | 功能使能（1=外设，0=GPIO 模式） |
| `GPIOEPU` | 0x69C | 上拉使能（1=10K 上拉使能） |
| `GPIOEPD` | 0x6A0 | 下拉使能（1=下拉） |
| `GPIOEDIR` | 0x68C | 方向（0=输出，1=输入） |
| `GPIOESET` | 0x680 | 置位输出（写 1 置位） |
| `GPIOECLR` | 0x684 | 清零输出（写 1 清零） |
| `GPIOE` | 0x688 | 读取引脚值（与方向无关） |

> **DIR 约定**（经 `test_gpio.c` PE4 测试验证通过）：
> - `GPIOEDIR bit = 0` = 输出
> - `GPIOEDIR bit = 1` = 输入

### 2.2 引脚功能（[bt892x_pinfunction.md §4.3](../bt892x_pinfunction.md) 第 128-129 行）

| 引脚 | 功能 | 默认方向 |
|---|---|---|
| PE6 | SCL（手动 GPIO 输出） | 输出 |
| PE7 | SDA（手动 GPIO，方向切换实现开漏） | 输出/输入 |

> **关键**：SDA 开漏模拟通过切换 DIR 实现 —— 我们驱动时设输出，从机应答时切输入释放 SDA。

---

## 3. 测试程序 1：test_i2c_gpio.c（AT24C02 功能验证）

### 3.1 引脚分配

| 引脚 | 角色 | 模式 |
|---|---|---|
| PE6 | SCL（手动 GPIO） | 始终输出 |
| PE7 | SDA（手动 GPIO） | 输出/输入切换（开漏模拟） |

### 3.2 Bit-bang I2C 原语

```c
// SCL 始终输出（我们控制时序）
#define SCL_OUT()       do { GPIOEDIR &= ~PE6_MASK; } while (0)
#define SCL_HIGH()      do { SCL_OUT(); GPIOESET = PE6_MASK; } while (0)
#define SCL_LOW()       do { SCL_OUT(); GPIOECLR = PE6_MASK; } while (0)

// SDA 切换输出/输入实现开漏
#define SDA_OUT()       do { GPIOEDIR &= ~PE7_MASK; } while (0)
#define SDA_IN()        do { GPIOEDIR |=  PE7_MASK; } while (0)  // 释放，由上拉拉高
#define SDA_OUT_HIGH()  do { SDA_OUT(); GPIOESET = PE7_MASK; } while (0)
#define SDA_OUT_LOW()   do { SDA_OUT(); GPIOECLR = PE7_MASK; } while (0)
#define SDA_READ()      ((GPIOE & PE7_MASK) ? 1 : 0)

// 时序延时（标准模式 ~80-100 kHz）
#define I2C_DELAY()      delay_us(5)   // SCL 半周期（高或低）
#define I2C_TSU()        delay_us(1)   // SCL 上升前的数据建立时间
```

### 3.3 PAD 初始化（附手册出处引用）

```c
// test_i2c_gpio.c 的 i2c_gpio_init_pads() 函数
GPIOEDE  |= PE6_7_MASK;     // 数字使能（手册 §3.2 GPIO 控制）
GPIOEFEN &= ~PE6_7_MASK;    // GPIO 模式（非外设，手册 §3.2）
GPIOEPU  |= PE6_7_MASK;     // 10K 上拉（手册 §3.2 + I2C 规范要求）
GPIOEPD  &= ~PE6_7_MASK;    // 关闭下拉
SDA_OUT_HIGH();              // SDA = 输出，驱动高
SCL_HIGH();                  // SCL = 输出，驱动高（注：SCL_HIGH 宏内已含 SCL_OUT）
delay_us(100);
```

### 3.4 I2C 事务原语

**START**（SCL 高时 SDA 下降）：

```c
static void i2c_gpio_start(void) {
    SDA_OUT_HIGH();
    SCL_HIGH();
    I2C_DELAY();
    SDA_OUT_LOW();     // SCL 高时 SDA 下降 = START
    I2C_DELAY();
    SCL_LOW();         // 拉低 SCL 准备时钟
    I2C_DELAY();
}
```

**STOP**（SCL 高时 SDA 上升）：

```c
static void i2c_gpio_stop(void) {
    SDA_OUT_LOW();
    I2C_DELAY();
    SCL_HIGH();
    I2C_DELAY();
    SDA_OUT_HIGH();     // SCL 高时 SDA 上升 = STOP
    I2C_DELAY();
}
```

**写字节**（MSB 先发，返回 ACK）：

```c
static bool i2c_gpio_write_byte(u8 data) {
    for (u8 i = 0; i < 8; i++) {
        if (data & 0x80) SDA_OUT_HIGH();
        else             SDA_OUT_LOW();
        I2C_TSU();        // 1µs 数据建立
        SCL_HIGH();
        I2C_DELAY();      // 5µs SCL 高
        SCL_LOW();
        I2C_DELAY();      // 5µs SCL 低
        data <<= 1;
    }
    // ACK 槽：释放 SDA 让从机驱动
    SDA_IN();
    I2C_TSU();
    SCL_HIGH();
    I2C_DELAY();
    bool ack = (SDA_READ() == 0);   // 0 = ACK
    SCL_LOW();
    I2C_DELAY();
    return ack;
}
```

**读字节**（返回字节，发 ACK 或 NAK）：

```c
static u8 i2c_gpio_read_byte(bool send_ack) {
    u8 val = 0;
    SDA_IN();        // 确保输入，释放 SDA
    for (u8 i = 0; i < 8; i++) {
        SCL_HIGH();
        I2C_DELAY();
        val = (val << 1) | SDA_READ();
        SCL_LOW();
        I2C_DELAY();
    }
    // 主机发 ACK 或 NAK
    SDA_OUT();
    if (send_ack) SDA_OUT_LOW();
    else          SDA_OUT_HIGH();
    I2C_TSU();
    SCL_HIGH();
    I2C_DELAY();
    SCL_LOW();
    I2C_DELAY();
    SDA_OUT_HIGH();
    return val;
}
```

### 3.5 高级事务

```c
// 探测地址（START + 地址 + STOP）
static bool i2c_gpio_probe_addr(u8 dev_addr7, bool is_read) {
    i2c_gpio_start();
    u8 ctl = (dev_addr7 << 1) | (is_read ? 1u : 0u);
    bool ack = i2c_gpio_write_byte(ctl);
    i2c_gpio_stop();
    return ack;
}

// 写 N 字节到 AT24C02 子地址
static bool i2c_gpio_write(u8 dev_addr7, u8 reg_addr,
                            const u8 *data, u8 len) {
    i2c_gpio_start();
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) goto fail;  // 地址+W
    if (!i2c_gpio_write_byte(reg_addr))               goto fail;  // 子地址
    for (u8 i = 0; i < len; i++) {
        if (!i2c_gpio_write_byte(data[i]))           goto fail;  // 数据字节
    }
    i2c_gpio_stop();
    return true;
fail:
    i2c_gpio_stop();
    return false;
}

// 读 N 字节（用重复起始 Sr）
static bool i2c_gpio_read(u8 dev_addr7, u8 reg_addr, u8 *buf, u8 len) {
    i2c_gpio_start();
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) goto fail;  // 地址+W
    if (!i2c_gpio_write_byte(reg_addr))               goto fail;  // 子地址
    i2c_gpio_start();                                             // Sr
    if (!i2c_gpio_write_byte((dev_addr7 << 1) | 1u)) goto fail;  // 地址+R
    for (u8 i = 0; i < len - 1; i++) {
        buf[i] = i2c_gpio_read_byte(true);    // ACK
    }
    buf[len - 1] = i2c_gpio_read_byte(false); // 最后一字节 NAK
    i2c_gpio_stop();
    return true;
fail:
    i2c_gpio_stop();
    return false;
}
```

### 3.6 测试流程

| 子测试 | 验证 | 期望 |
|---|---|---|
| **Test 1** | START + 0xA0 + STOP 探测 | ACK |
| **Test 2** | 扫描 0x08~0x77 | AT24C02 @ 0x50 ACK |
| **Test 3** | 写 AT24C02[0x00] = 0x55 | ACK |
| **Test 4** | 读 AT24C02[0x00] | 读到 0x55 → WRITE-READ PASS |
| **Test 5** | 写读 4 字节 pattern (0xDE 0xAD 0xBE 0xEF) | 全部匹配 → PATTERN PASS |

### 3.7 实测结果（用户验证）

```
[TEST] [Test 1] Single address probe @ 0x50
[TEST]   Probe 0x50 result: ACK
[TEST] [Test 2] Address scan 0x08..0x77
[TEST]   Found device at 0x50
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

**结论**：5 个子测试全部通过。GPIO bit-bang I2C 实现正确与 AT24C02 通信。

### 3.8 开发过程中的关键 Bug 修复

**Bug**：最初的 `SCL_HIGH()` 宏只执行 `GPIOESET = PE6_MASK`，**没有将 PE6 设为输出模式**。如果 PE6 的 DIR 复位后是输入态（默认），SET 操作对引脚无任何效果。

**修复**：新增 `SCL_OUT()` 宏，将 `GPIOEDIR &= ~PE6_MASK` 设为输出模式。`SCL_HIGH()` 和 `SCL_LOW()` 都先调用 `SCL_OUT()`。

```c
// 修复前（有 bug）：
#define SCL_HIGH()  do { GPIOESET = PE6_MASK; } while (0)

// 修复后：
#define SCL_OUT()   do { GPIOEDIR &= ~PE6_MASK; } while (0)
#define SCL_HIGH()  do { SCL_OUT(); GPIOESET = PE6_MASK; } while (0)
```

---

## 4. 测试程序 2：test_i2c_gpio_la.c（逻辑分析仪时序验证）

### 4.1 目的

验证 bit-bang I2C 产生正确的 SCL/SDA 波形。PE6/PE7 接逻辑分析仪（可同时接 AT24C02），跑 60 次 START + 地址 + STOP 事务。LA 抓取波形 burst。

### 4.2 硬件连接

```
LA:  CH1 → PE6 (SCL)
     CH2 → PE7 (SDA)
     GND → GND

AT24C02: 可接可不接（bit-bang 无论从机是否存在都驱动 SCL）
```

### 4.3 Burst 代码

```c
#define BURST_COUNT  60
static void la_burst(void) {
    for (u32 i = 0; i < BURST_COUNT; i++) {
        // START
        SDA_OUT_HIGH();
        SCL_HIGH();
        delay_us(5);
        SDA_OUT_LOW();       // SCL 高时 SDA 下降 = START
        delay_us(5);
        SCL_LOW();
        delay_us(5);

        // 地址字节 0xA0 (0x50 << 1 | W)
        u8 ctl = (0x50 << 1) | 0;
        for (u8 b = 0; b < 8; b++) {
            if (ctl & 0x80) SDA_OUT_HIGH();
            else             SDA_OUT_LOW();
            delay_us(1);
            SCL_HIGH();  delay_us(5);
            SCL_LOW();   delay_us(5);
            ctl <<= 1;
        }
        // ACK 槽
        SDA_IN();
        delay_us(1);
        SCL_HIGH();  delay_us(5);
        SCL_LOW();   delay_us(5);

        // STOP
        SDA_OUT_LOW();
        delay_us(5);
        SCL_HIGH();
        delay_us(5);
        SDA_OUT_HIGH();
        delay_us(5);
    }
}
```

### 4.4 时序

每个 SCL 时钟（数据位）：
- T_su_dat = 1 µs
- T_high = 5 µs
- T_low = 5 µs
- **每个 SCL 时钟 = 11 µs，SCL 频率 ≈ 91 kHz**

### 4.5 LA 预期波形

每个事务应该看到：

```
SCL :  ___↑__↑__↑__↑__↑__↑__↑__↑__↑__↑___________↑__
SDA :  _____\___________________________↑___________\___________
       ↑   0xA0 = 10100000（MSB 先）   ↑  ACK    ↑ STOP
       START（SCL 高时 SDA 下降）
```

**关键检查**：
1. START：SCL 高时 SDA 下降 ✓
2. 8 个数据位 + 8 个 SCL 时钟（0xA0 = 10100000）
3. 第 9 个 SCL 时钟（ACK 槽）—— AT24C02 应将 SDA 拉低
4. STOP：SCL 高时 SDA 上升 ✓

### 4.6 实测结果

用户确认：修复后 LA 上能看到 SCL 脉冲。SCL 周期约 11 µs ≈ 91 kHz，与预期一致。

---

## 5. 完整寄存器表（仅 GPIO PE，**无 IIC 控制器**）

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `GPIOEDE` | 0x690 | `\|= PE6_7_MASK` 数字使能 | §3.2 |
| `GPIOEFEN` | 0x694 | `&= ~PE6_7_MASK` GPIO 模式（非外设） | §3.2 |
| `GPIOEPU` | 0x69C | `\|= PE6_7_MASK` 10K 上拉（I2C 规范要求） | §3.2 |
| `GPIOEPD` | 0x6A0 | `&= ~PE6_7_MASK` 关闭下拉 | §3.2 |
| `GPIOEDIR` | 0x68C | 在 bit-bang 中切换：SCL 始终输出，SDA 输出/输入 | §3.2 |
| `GPIOESET` | 0x680 | 驱动高（写 1） | §3.2 |
| `GPIOECLR` | 0x684 | 驱动低（写 1） | §3.2 |
| `GPIOE` | 0x688 | 读取引脚值（ACK 期间 SDA 检测） | §3.2 |

> **未使用 IIC 控制器寄存器**——纯 GPIO 软件模拟。

---

## 6. 硬件 vs 软件 I2C 对比

| 项目 | 硬件 IIC（[test_hard_i2c.md](test_hard_i2c.md)） | 软件 bit-bang（本文件） |
|---|---|---|
| 时钟源 | 内部 IIC 控制器（RC2M 或 XOSC26M per §8.1） | 软件 `delay_us()` |
| 频率精度 | 精确（芯片控制） | 近似（依赖延时精度） |
| 典型 SCL 频率 | ~100 kHz（POSDIV=19） | ~91 kHz（11µs SCL 时钟） |
| CPU 开销 | 低（硬件完成） | 高（CPU 一位一位切换 GPIO） |
| 引脚灵活性 | 受限于 FUNCMCON2 IIC Group | 任何 GPIO 都可做 SCL/SDA |
| 使用场景 | 生产、低 CPU 负载 | 测试、调试、备选方案 |
| 与 AT24C02 接口相同？ | 是 | 是 |

---

## 7. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| Test 1 ACK 持续 NAK | (a) SCL/SDA 未设输出模式 (b) AT24C02 未接 (c) VCC 缺失 | (a) 添加 `SCL_OUT()`（已修复）；检查 `GPIOEDIR`；(b) 检查接线；(c) 测 VCC=3.3V |
| 全部地址 NAK | (a) AT24C02 未上电 (b) 上拉太弱 (c) SDA 卡低/高 | (a) 验证 VCC；(b) 加外部 4.7K；(c) 检查 SDA 方向切换是否正确 |
| 写成功但读不一致 | AT24C02 写周期未完成 | 写后加 `delay_ms(10)` 再读 |
| LA 上 SCL 卡高 | SCL DIR=输入（SET/CLR 无效） | 确保 `SCL_OUT()` 被调用（已修复） |
| SDA 卡低 | SDA 始终输出（从未释放给从机驱动） | 确保 ACK 槽调用 `SDA_IN()` |
| 编译错误 `undeclared GPIOxxx` | `header/sfr.h` 未包含 | 验证 `#include "test_common.h"`（已含 sfr.h） |

---

## 8. 工程意义

- **测试文件**：`test_i2c_gpio.c`（5 个子测试）+ `test_i2c_gpio_la.c`（LA 时序）
- **与硬件 IIC 对比**：见 [test_hard_i2c.md](test_hard_i2c.md)
- **使用场景**：任意 GPIO 验证 I2C 协议、IIC 控制器不可用时备选、学习 I2C 协议
- **不需要 IIC 控制器**：纯软件 GPIO bit-bang
- **手册依据**：每个 GPIO 寄存器都引用 [BT892X_UserManual_Driver.md §3.2](../BT892X_UserManual_Driver.md) 和 [sfr.h](../../smart_mini/header/sfr.h)

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_i2c_gpio.c` | AT24C02 功能测试（bit-bang） |
| `smart_mini/test/test_i2c_gpio.h` | 头文件 |
| `smart_mini/test/test_i2c_gpio_la.c` | 逻辑分析仪时序测试（bit-bang） |
| `smart_mini/test/test_i2c_gpio_la.h` | 头文件 |
| `smart_mini/test/test_common.h` | 共用 `TEST_LOG` 宏 |
| `smart_mini/main.c` | 入口（按宏开关调用对应测试） |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 寄存器宏定义（第 359-362 行 IIC 寄存器，第 450-462 行 GPIO PE） |
| `docs/BT892X_UserManual_Driver.md` | 芯片寄存器手册（§3.2 GPIO，§8.1 IIC 特性） |
| `docs/bt892x_pinfunction.md` | 引脚功能定义（§4.3 PE6/PE7） |