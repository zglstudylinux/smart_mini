# BT892X I2C 外设技术文档（硬件 IIC + 软件 bit-bang）

> **适用芯片**：中科蓝讯 BT892X（32-bit RISC-V SoC）
> **工程**：smart_mini（CodeBlocks 工程 `app.cbp`，寄存器裸操作，无独立 driver 层）
> **寄存器映射**：`smart_mini/header/sfr.h`
> **手册**：`docs/BT892X_UserManual_Driver.md`（第 8 章 IIC）、`docs/bt892x_pinfunction.md`（第 4.3 / 8.5 节）
> **被测从机**：AT24C02 EEPROM（7-bit 地址 0x50，A0=A1=A2=GND）

---

## 1. 外设概述与本工程用途、涉及文件清单

### 1.1 外设概述

本工程用 **两种** 方式在 BT892X 上实现 I2C 主机（master）协议，作为芯片 I/O 能力的回归测试与总线协议验证：

| 实现路径 | 使用资源 | 适用场景 | 关键差异 |
|:---|:---|:---|:---|
| **硬件 IIC 控制器** | BT892X 内置 IIC master 外设 + PE6/PE7（Group G5） | 生产代码、低 CPU 占用、精确 SCL 周期 | 时序由芯片产生，CPU 仅配置寄存器后等待 DONE |
| **软件 bit-bang** | 纯 GPIO（PE6=SCL、PE7=SDA）通过 delay_us 模拟时序 | IIC 控制器被占用、协议教学、任意 GPIO 模拟 I2C | CPU 一位一位手动翻转引脚；SDA 方向切换实现开漏 |

两种实现 **使用同一对外引脚 PE6/PE7**（同一 Group），但任一时刻只能跑其中一种——典型做法是把四个 `TEST_I2C_*_EN` 宏分时打开、单独烧写、单独验证。

### 1.2 本工程用途

- **回归测试**：验证芯片 IIC 控制器与 GPIO 时序能力
- **协议教学**：bit-bang 路径可作为学习 I2C 总线规范（START/STOP/ACK/重复起始 Sr）的完整可读实现
- **校验总线时序**：配合逻辑分析仪（LA）实测 SCL 频率，验证手册 §8.2 波特率公式
- **板级 bring-up**：验证 AT24C02 模块、I2C 上拉、引脚映射是否正确

### 1.3 涉及文件清单（相对工程根）

| 文件 | 角色 |
|:---|:---|
| `smart_mini/test/test_i2c.c` | **硬件 IIC** AT24C02 功能测试（5 个子测试：probe/scan/write/单字节读/4 字节 pattern） |
| `smart_mini/test/test_i2c.h` | 硬件 IIC 测试入口声明 |
| `smart_mini/test/test_i2c_la.c` | **硬件 IIC** 逻辑分析仪时序测试（连续 burst 事务，给 LA 抓 SCL 周期） |
| `smart_mini/test/test_i2c_la.h` | LA 测试入口声明 |
| `smart_mini/test/test_i2c_gpio.c` | **软件 bit-bang** AT24C02 功能测试（与硬件版完全对称的 5 子测试） |
| `smart_mini/test/test_i2c_gpio.h` | bit-bang 测试入口声明 |
| `smart_mini/test/test_i2c_gpio_la.c` | **软件 bit-bang** 逻辑分析仪时序测试 |
| `smart_mini/test/test_i2c_gpio_la.h` | bit-bang LA 测试入口声明 |
| `smart_mini/main.c` | 含 `TEST_I2C_EN`/`TEST_I2C_LA_EN`/`TEST_I2C_GPIO_EN`/`TEST_I2C_GPIO_LA_EN` 四个入口宏 |
| `smart_mini/header/sfr.h` | SFR 寄存器宏定义（IIC 寄存器位于第 359–362 行） |
| `smart_mini/test/test_common.h` | 提供 `TEST_LOG` 宏（带 `[TEST]` 前缀）和 GPIO 通用操作宏 |
| `docs/BT892X_UserManual_Driver.md` | 寄存器手册（§8 IIC，第 544–615 行） |
| `docs/bt892x_pinfunction.md` | 引脚复用表（§4.3 PE6/PE7，第 128–129 行；§8.5 IIC 信号，第 233–239 行） |
| `docs/test_hard_i2c.md` | 硬件 IIC 实测报告（2026-07-17） |
| `docs/test_soft_i2c.md` | 软件 bit-bang 实测报告（2026-07-17） |

### 1.4 AT24C02（被测从机）速记

| 字段 | 值 |
|:---|:---|
| 7-bit 地址 | `0x50`（A0=A1=A2=GND） |
| 控制字节 W | `0xA0`（`(0x50<<1)\|0`） |
| 控制字节 R | `0xA1`（`(0x50<<1)\|1`） |
| 写后等待 | AT24C02 写周期 ≤ 5 ms，工程实测加 `delay_ms(10)` 更稳 |
| 时序 | 支持 Standard-mode 100 kHz |

---

## 2. 涉及寄存器逐个说明

所有地址由 `SFRn_BASE + offset*4` 计算得到（基地址在 `sfr.h` 第 19–35 行）。本节列出 **硬件 IIC 控制器寄存器、时钟门、FUNCMCON、GPIO PE 端口** 全部被 bit 操作过的寄存器。

### 2.1 硬件 IIC 控制器寄存器组（手册 §8.2）

| 寄存器名 | 地址 | sfr.h 行号 | 手册章节 | 关键位域（手册表） | 含义 |
|:---|:---|:---|:---|:---|:---|
| `IICCON0` | `0x51C` | 359 | §8.2 表 1（第 554–568 行） | bit0 `IIC_EN`/bit1 `INTEN`/bit3:2 `HOLDCNT`/bit9:4 `POSDIV`/bit27 `CLR_ALL`/bit28 `KS`/bit29 `CLR_DONE`/bit30 `ACKSTATUS`/bit31 `DONE` | IIC 主控使能 / 中断 / SCL 保持 / SCL 高电平分频 / 清状态 / 启动 / 清完成 / 从机 ACK / 事务完成 |
| `IICCON1` | `0x520` | 360 | §8.2 表 2（第 570–584 行） | bit2:0 `DATA_CNT`/bit3 `START0_EN`/bit4 `CTL0_EN`/bit5 `ADR0_EN`/bit6 `ADR1_EN`/bit7 `START1_EN`/bit8 `CTL1_EN`/bit9 `RDAT_EN`/bit10 `WDAT_EN`/bit11 `STOP_EN`/bit12 `TXNAK_EN` | 事务动作使能位图 + 数据字节数 |
| `IICCMDA` | `0x524` | 361 | §8.2 表 3（第 586–593 行） | bit7:0 `CTL0`/bit15:8 `ADR0`/bit23:16 `ADR1`/bit31:24 `CTL1` | 命令 + 地址复用：4 个独立 8-bit 字段 |
| `IICDATA` | `0x528` | 362 | §8.2 表 4（第 595–602 行） | bit7:0 `DATA0`/bit15:8 `DATA1`/bit23:16 `DATA2`/bit31:24 `DATA3` | 最多 4 字节数据（发送/接收均为 DATA0 先） |

**核心公式**（手册 §8.2 末尾）：
```
IICCLK = source_clk / (preclkdiv + 1)
SCL    = IICCLK / (posdiv + 1)
```
其中 `POSDIV` = `IICCON0[9:4]`，即 `POSDIV` 字段写入值 N 时实际分频系数 = N+1。

### 2.2 时钟门 `CLKGAT2`（手册 §8.3 step 1，用户提供的 CLKGAT 表）

| 寄存器名 | 地址 | sfr.h 行号 | 手册章节 | 关键位域 | 本工程使用 |
|:---|:---|:---|:---|:---|:---|
| `CLKGAT2` | `0x0F8` | 101 | 用户提供的 CLKGAT 表（手册未逐位定义） | bit0 = IIC 时钟门 | `CLKGAT2 \|= BIT(0)` 开 IIC 时钟 |

> 注：sfr.h 第 101 行将 `CLKGAT2` 定义为 `SFR0_BASE + 0x3E*4 = 0x000 + 0xF8`，对应绝对地址 `0x0F8`。test_hard_i2c.md 表里写为 `0x3E4`，与 sfr.h 实际值不一致；本文档以 sfr.h 为准。

### 2.3 引脚复用 `FUNCMCON2`（手册 §3.3）

| 寄存器名 | 地址 | sfr.h 行号 | 手册章节 | 关键位域 | 本工程使用 |
|:---|:---|:---|:---|:---|:---|
| `FUNCMCON2` | `0x024` | 46 | §3.3 FUNCMCON2（第 179–186 行） | bit24:27 = IIC Group（0001=G1…0101=G5，1111=清除） | bit24:27 写入 `0x5` → Group G5 |

### 2.4 GPIO PE 端口寄存器组（手册 §3.2，sfr.h 第 450–462 行）

地址皆由 `SFR6_BASE = 0x600` + offset*4 算出：

| 寄存器名 | 地址 | sfr.h 行号 | 手册章节 | 本工程使用 |
|:---|:---|:---|:---|:---|
| `GPIOESET` | `0x680` | 450 | §3.2 GPIO 通用（位写 1 置位） | `SCL_HIGH/SDA_OUT_HIGH` 宏 |
| `GPIOECLR` | `0x684` | 451 | §3.2 | `SCL_LOW/SDA_OUT_LOW` 宏 |
| `GPIOE` | `0x688` | 452 | §3.2（读 = 当前引脚值） | `SDA_READ()` 宏 |
| `GPIOEDIR` | `0x68C` | 453 | §3.2（0=输出，1=输入） | bit-bang 中 SDA 切换 DIR 模拟开漏 |
| `GPIOEDE` | `0x690` | 454 | §3.2（数字使能，1=数字） | 硬件 IIC 和 bit-bang 都置位 PE6/PE7 |
| `GPIOEFEN` | `0x694` | 455 | §3.2（功能映射，1=外设） | 硬件 IIC 置位 / bit-bang 清零 |
| `GPIOEPU` | `0x69C` | 457 | §3.2（10K 上拉使能） | 两种实现都置位 PE6/PE7（开漏上拉） |
| `GPIOEPD` | `0x6A0` | 458 | §3.2（10K 下拉使能） | 都清零 PE6/PE7 |

### 2.5 其它会被读/写的全局 SFR

| 寄存器名 | 地址 | sfr.h 行号 | 用法 | 出处 |
|:---|:---|:---|:---|:---|
| `TMR2CNT` | `0x0F4`（SFR0+0x3D*4） | 99 | 32 位 free-running 计数器，`delay_us` 与 `wait_done` 计时 | main.c `timer2_init`（第 100–106 行） |
| `UART0CON`/`UART0DATA` | sfr.h 54/57 | — | 串口打印（`TEST_LOG` 宏底层 = `printf` → `uart_putchar`） | main.c 第 92–97 行 |

---

## 3. 引脚定义与复用

### 3.1 使用到的引脚（本工程所有 I2C 测试均使用同一对引脚）

| PAD | 功能 | 复用的 I2C 信号 | Group | 与其它外设的复用冲突 |
|:---|:---|:---|:---|:---|
| **PE6** | SCL | IIC_CLK-G5 | G5 | 同时承载 PWM-T4-G1、IIS-LRCLK-G2/G3、SPI1CLK-G4、SDCLK-G3、RX0-G4、HSTRX-G9、FMOSC-G6、DVP_VSYNC、TMR3CAP_G7/IR_G7、AUXL2、ADC8 |
| **PE7** | SDA | IIC_DAT-G5 | G5 | 同时承载 PWM2-T4-G1、IISDO-G2/G3、SPI1DO-G4、SDDAT0-G3、TX0-G4(RX)、HSTRX-G4、IIC_DAT-G5、TMR4CAP_G1/IR_G8、AUXR2、ADC9 |

> **唯一性约束**：对照 `bt892x_pinfunction.md` §4.3 第 128–129 行——
> - PE6 行 IIC 列标注 `"IIC_CLK-G5/G6"`
> - PE7 行 IIC 列标注 `"IIC_DAT-G5"`（只有 G5，G6 SDA 不在 PE7）
>
> 因此 **G5 是同时把 PE6 配成 SCL、PE7 配成 SDA 的唯一 Group**。任何想用 BT892X 硬件 IIC 接 PE6/PE7 的代码都必须将 `FUNCMCON2[24:27]` 设为 `0x5`。

### 3.2 FUNCMCONx 映射值

| 寄存器 | 字段 | 写入值 | 含义 | 手册依据 |
|:---|:---|:---|:---|:---|
| `FUNCMCON2[27:24]` | IICMAP | `0x5` | 选 G5 → PE6=SCL、PE7=SDA | bt892x_pinfunction.md 第 86–92 行表 |

> 注：`FUNCMCON2` 的其它位域（TMR3MAP、TMR4MAP、TMR5MAP、TMR3CPTMAP、IISMAP、IR_MAP、DVP_MAP）与本外设无关，本工程不动。

### 3.3 复用冲突约束（在 multi-peripheral 场景下需要留意）

| 如果同时打开 | 会冲突的引脚 | 解决方案 |
|:---|:---|:---|
| I2C + SPI 软件/硬件 loop/wave/w25q64/timing 测试 | PE6/PE7 全部被 SPI 占用（CLK/MOSI/…） | **一次只开一个** TEST_*_EN（main.c 第 56–67 行的注释已明文约束） |
| I2C + UART2 | PE7 不会被 UART2 占用（UART2 仅 PB1/PB2/VUSB），但 PB1=PB2 与 PE6/PE7 无交集 → 可同开 | — |
| I2C + ADKEY | PE7=ADKEY 功能默认存在，但 I2C 期间不会触发 ADKEY 扫描；实测无冲突 | — |

> 工程约束（来自 main.c 第 56–71 行注释）：`TEST_I2C_*_EN` 与 `TEST_SPI_*_EN` 互斥（共用 PE4/PE5/PE6/PE7），与 `TEST_UART_RECV_EN` 同开时不影响 bus。

---

## 4. 初始化原理

### 4.1 为什么需要这五步（硬件 IIC）

| 步骤 | 因果链 | 关键依据 |
|:---|:---|:---|
| **① 开 IIC 时钟门 `CLKGAT2 \|= BIT(0)`** | BT892X 大部分外设的时钟独立门控，未开时钟门时写 IIC 寄存器无效（手册未明示，但 `CLKGAT` 表确认） | 手册 §8.3 step 1 + 用户提供 CLKGAT 表 |
| **② PE6/PE7 PAD 数字 + 上拉** | I2C 规范要求 SDA/SCL 高电平由上拉电阻产生（开漏）。设置 `DE=1`(数字使能)+`FEN=1`(交给 IIC 外设)+`PU=1`(内部 10K 上拉)+`DIR=0`(输出)，并清 `PD` 防止上下拉互冲 | 手册 §3.2 表 + §8.3 "SDA 设置上拉使能" |
| **③ `FUNCMCON2[27:24]=5`** | BT892X 每个 PAD 都可被多个外设复用，必须由 FUNCMCON 选择 Group；本工程选 **唯一** 能同时把 PE6/PE7 配成 SCL/SDA 的 G5 | 手册 §3.3 + bt892x_pinfunction.md §4.3 §8.5 |
| **④ 配置 `IICCON0`** | `POSDIV=19` (bit9:4) 决定 SCL 高电平分频 20；`HOLDCNT=0`（手册表）保持默认；`IIC_EN=1`（bit0）使能主控；随后 `CLR_ALL`（bit27）清状态 | 手册 §8.2 + §8.3 step 2/3 |
| **⑤ `delay_us(100)`** | 手册未强制要求，但 IIC 控制器从 reset 到状态稳定需要时钟建立时间；实测保险 | 工程经验，test_i2c.c 第 89 行 |

### 4.2 时钟源为何用默认值

手册 §8.1 写明 IIC 时钟源支持 "RC2M 或 XOSC26M"，但 §8.2 公式 `source_clk / (preclkdiv+1)` 中的 `preclkdiv` 没有在手册中定义寄存器位置和默认值。test_i2c.c **直接使用 `preclkdiv` 默认值，不修改任何 preclkdiv 寄存器**——实测 LA 测得 SCL 周期 ≈ 7.94 µs，对应 IICK ≈ 2.52 MHz（详见 §7），与 RC2M 标称 2 MHz / 实际校准值在合理范围内。

### 4.3 为什么 bit-bang 路径需 FEN=0

`GPIOEFEN` bit=1 时 PAD 交给外设控制；bit=0 时由 CPU 直接写 `GPIOxSET/CLR` 控制电平。bit-bang 路径必须 **清零** PE6/PE7 的 `FEN` 位（`GPIOEFEN &= ~PE6_7_MASK`），否则 SCL/SDA 由 IIC 控制器（或别的外设）接管，CPU 无法直接驱动。

### 4.4 为什么 SDA 要切换 DIR（bit-bang 的开漏模拟）

BT892X 的 GPIO 没有真正的开漏模式（手册 §3.2 GPIO 仅有标准 CMOS 推挽）。模拟 I2C 开漏的方法是：
- 主机要发 '1'：`PE7_DIR=输出(0)` + `GPIOESET` → 推挽输出高；线路上拉到高
- 主机要发 '0'：`PE7_DIR=输出(0)` + `GPIOECLR` → 推挽输出低
- 主机要 **读** 从机 ACK：把 `PE7_DIR=输入(1)`，此时 CPU 不再驱动，由内部 10K 上拉 +（可能的）外部上拉维持高电平；从机把 SDA 拉低则 CPU 读到 0
- 释放时（STOP/Sr/ACK 槽）：`SDA_IN()` 把方向切到输入，对外表现等效"开漏释放"

SCL 不需要切方向——SCL 始终由主机驱动（`SCL_OUT()` 宏一直把 `PE6_DIR=输出(0)`）。

---

## 5. 初始化操作步骤

下面给出 **手册 §8.3 step 1~3** 与工程实现的对照步骤；适用于硬件 IIC。

### 硬件 IIC 初始化步骤（test_i2c.c `test_i2c_init()`）

1. **开 IIC 时钟门**：`CLKGAT2 \|= BIT(0)`（手册 §8.3 step 1）
2. **PE6/PE7 PAD 数字使能 + 外设功能 + 上拉**：
   - `GPIOEDE \|= PE6_7_MASK`（数字 IO）
   - `GPIOEFEN \|= PE6_7_MASK`（让 IIC 接管 PE6/PE7）
   - `GPIOEPU \|= PE6_7_MASK`（10K 上拉）
   - `GPIOEPD &= ~PE6_7_MASK`（关下拉）
   - `GPIOEDIR &= ~PE6_7_MASK`（输出方向）
3. **选择 IIC Group G5**：`FUNCMCON2 = (FUNCMCON2 & ~(0xFu<<24)) \| (0x5u<<24)`
4. **IICCON0 主配置**（手册 §8.2 表 + §8.3 step 2/3）：
   - `HOLDCNT = 0` → bit3:2 写入 `0`
   - `POSDIV = 19` → bit9:4 写入 `19` → 实际分频 = 20
   - `IIC_EN = 1` → bit0 置 1
   - 整字一次写入：`IICCON0 = (0u<<2) \| (19u<<4) \| IIC_EN`
5. **清状态**：`IICCON0 \|= IIC_CLR_ALL`（bit27 写 1 清所有状态）
6. **稳定延时**：`delay_us(100)`

### 硬件 IIC 单次事务步骤（手册 §8.3 step 4~9 + 实际代码顺序）

7. **加载命令/地址**：`IICCMDA = ...`，CTL0[7:0]=`(dev_addr<<1)|R/W`，ADR0[15:8]=子地址（如有），CTL1[31:24]=读事务时的 `(dev_addr<<1)|1`（重复起始第二地址）
8. **加载数据**：`IICDATA = ...`，DATA0[7:0]=第一个字节（小端放入 data_word 后整字写入）
9. **加载动作序列**：`IICCON1 = START0_EN\|CTL0_EN\|ADR0_EN\|WDAT_EN\|STOP_EN\|(len&0x7)` 等
10. **kick start**：`IICCON0 \|= IIC_KS`（bit28 写 1）
11. **轮询 `DONE`**：等 `IICCON0` bit31=1（中断方式可改 `INTEN=1`，本工程用 polling + timeout）
12. **判读 `ACKSTATUS`**：`ACKSTATUS=0` → ACK；`=1` → NAK
13. **清完成**：`IICCON0 \|= IIC_CLR_DONE`（bit29 写 1）

### 软件 bit-bang 初始化步骤（test_i2c_gpio.c `i2c_gpio_init_pads()`）

1. **PE6/PE7 PAD 数字使能 + GPIO 模式 + 上拉**：
   - `GPIOEDE \|= PE6_7_MASK`（数字 IO）
   - `GPIOEFEN &= ~PE6_7_MASK`（**GPIO 模式，不交给外设**）
   - `GPIOEPU \|= PE6_7_MASK`（10K 上拉）
   - `GPIOEPD &= ~PE6_7_MASK`（关下拉）
2. **总线上拉到 idle（高）**：`SDA_OUT_HIGH(); SCL_HIGH();`（这两个宏都先调 `SDA_OUT()/SCL_OUT()` 把 DIR 设为输出）
3. **稳定延时**：`delay_us(100)`

### 软件 bit-bang 单次事务步骤（参考手册 §8.3 协议层语义）

4. **START**：`i2c_gpio_start()` —— SCL 高时 SDA 拉低（每次延迟 5 µs）
5. **写地址字节**：MSB 先发 8 位 `i2c_gpio_write_byte(ctl)`，第 9 个 SCL 时钟采样从机 ACK
6. **写子地址**：`i2c_gpio_write_byte(reg_addr)`（读事务前）
7. **写数据**：每个字节都 `i2c_gpio_write_byte(data[i])`
8. **Sr 重复起始**（读事务）：`i2c_gpio_start()` 再次发 START
9. **写地址+R**：再次 `i2c_gpio_write_byte((dev_addr<<1)|1)`
10. **读数据**：`i2c_gpio_read_byte(true)` 接收中间字节发 ACK；最后一字节用 `i2c_gpio_read_byte(false)` 发 NAK
11. **STOP**：`i2c_gpio_stop()` —— SCL 高时 SDA 拉高

---

## 6. 代码详解

下面把关键代码段按行号区间列出，并逐条解释每条寄存器操作的目的。

### 6.1 硬件 IIC：初始化（test_i2c.c 第 69–90 行）

```c
69  static void test_i2c_init(void)
70  {
71      // Sec.8.3 step 1: open IIC clock gate (CLKGAT2[0] = IIC)
72      CLKGAT2 |= BIT(0);
73
74      // PE6/PE7 PAD: digital IO + pull-up + function select
75      GPIOEDE   |=  PE6_7_MASK;   // digital enable
76      GPIOEFEN  |=  PE6_7_MASK;   // peripheral function (let IIC own)
77      GPIOEPU   |=  PE6_7_MASK;   // internal 10K pull-up (Sec.8.3 requires SDA pull-up)
78      GPIOEPD   &= ~PE6_7_MASK;
79      GPIOEDIR  &= ~PE6_7_MASK;
80
81      // FUNCMCON2[24:27] = IIC Group G5 (Sec.4.3 + Sec.3.3)
82      FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);
83
84      // Sec.8.3 step 2/3: IICCON0 main config (manual Sec.8.2 table)
85      IICCON0 = (0u  << 2)    // HOLDCNT = 0 (manual table)
86              | (19u << 4)    // POSDIV = 19
87              | IIC_EN;       // IIC_EN = 1 (manual table)
88      IICCON0 |= IIC_CLR_ALL;    // manual bit 27
89      delay_us(100);
90  }
```

**逐行解释：**

- L72 `CLKGAT2 |= BIT(0)`：手册 §8.3 step 1，门控寄存器 bit0 是 IIC 模块的电源开关；不打开后续寄存器写无效。
- L75 `GPIOEDE |= PE6_7_MASK`：手册 §3.2 表 3.2-1 `GPIOxDE` 把 PAD 切到数字域，避免模拟域干扰。复位默认 0xFF = 全数字，工程中是冗余写。
- L76 `GPIOEFEN |= PE6_7_MASK`：手册 §3.2 `GPIOxFEN` 决定 PAD 由 GPIO 控制还是由外设控制。硬件 IIC 路径必须 =1，把 PE6/PE7 交给 IIC 控制器。
- L77–78 `PU/PD`：手册 §8.3 强调 SDA 需要上拉；本工程用内部 10K（手册 §9.2 表 9-5 `RPUP0 = 10KΩ 典型`）。
- L79 `GPIOEDIR &= ~PE6_7_MASK`：IIC 控制器需要 PAD 处于输出态（手册 §3.2 `DIR=0`）；其实一旦 `FEN=1` DIR 是否输出已不重要，但显式设 0 是稳妥做法。
- L82：`FUNCMCON2[24:27]=0x5` 选择 **G5**，原因见 §3.1（PE6/PE7 同时承载 IIC 必须 G5）。
- L85–87：手册 §8.2 IICCON0 表三个字段：HOLDCNT=0、POSDIV=19（分频 20）、IIC_EN=1。整字一次写是把未定义位清 0 的好习惯。
- L88 `IICCON0 |= IIC_CLR_ALL`：手册 §8.2 bit27 写 1 清所有状态；这是 IIC 上电后的标准初始化。
- L89 `delay_us(100)`：经验值，让时钟/控制器建立稳定。

### 6.2 硬件 IIC：探测地址（test_i2c.c 第 95–113 行）

```c
95  static bool test_iic_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us)
96  {
97      // Sec.8.3 step 4: load command/address (CTL0 = address + R/W)
98      IICCMDA = (u8)((dev_addr7 << 1) | (is_read ? 1u : 0u));
99
100     // Sec.8.3 step 6: load action sequence (START + CTL0 + STOP, no data)
101     IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_STOP_EN | 0;
102
103     // Sec.8.3 step 7: kick start
104     IICCON0 |= IIC_KS;
105
106     if (!test_iic_wait_done(timeout_us)) {
107         IICCON0 |= IIC_CLR_DONE;
108         return false;
109     }
110     bool ack = !(IICCON0 & IIC_ACKSTATUS);  // manual Sec.8.2 ACKSTATUS table
111     IICCON0 |= IIC_CLR_DONE;               // manual step 9
112     return ack;
113 }
```

**逐行解释：**

- L98 `IICCMDA`：只用到 CTL0[7:0]，所以只填了低 8 bit；高 24 bit 写 0。`dev_addr7` 是 7-bit 地址，左移 1 位后 OR 上 R/W 位，正好就是手册 §8.2 IICCMDA 表 CTL0 字段的语义。
- L101 `IICCON1`：动作使能位 START0+CTL0+STOP 是"探测事务"——只发一个地址字节就 STOP（不写子地址，不写数据）。`DATA_CNT=0` 因为不收发数据。
- L104 `IICCON0 |= IIC_KS`：手册 §8.3 step 7 kick start，bit28 写 1。
- L106–109 `test_iic_wait_done()`（56–65 行定义）：用 `TMR2CNT` 做 us 计时轮询 `IICCON0[31]`，超时返回 false。
- L110 `ACKSTATUS`：bit30 = 0 表示从机 ACK，=1 表示 NAK；返回 `ack = !ACKSTATUS` 把"读完位"变成 bool。
- L111 `CLR_DONE`：手册 §8.3 step 9 每次事务后必须清，否则下一次事务 `DONE` 不重新拉起。

### 6.3 硬件 IIC：写 N 字节（test_i2c.c 第 118–148 行）

```c
118 static bool test_iic_write(u8 dev_addr7, u8 reg_addr,
119                             const u8 *data, u8 len, u32 timeout_us)
120 {
121     if (len == 0 || len > 4) return false;     // 限制：IICDATA 只有 4 byte 字段
122
123     // Sec.8.3 step 4: load IICCMDA (CTL0=address+W, ADR0=sub-address)
124     IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)             // CTL0 [7:0]
125             | ((u32)reg_addr << 8);                     // ADR0 [15:8]
126
127     // Sec.8.3 step 5: load IICDATA (DATA0 = first byte sent)
128     u32 data_word = 0;
129     for (u8 i = 0; i < len; i++) {
130         data_word |= ((u32)data[i]) << (i * 8);
131     }
132     IICDATA = data_word;
133
134     // Sec.8.3 step 6: action sequence START + CTL0 + ADR0 + WDAT + STOP
135     IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_ADR0_EN
136             | IIC_WDAT_EN | IIC_STOP_EN
137             | (len & 0x7);                              // DATA_CNT
138
139     IICCON0 |= IIC_KS;
...
145     bool ack = !(IICCON0 & IIC_ACKSTATUS);
146     IICCON0 |= IIC_CLR_DONE;
147     return ack;
148 }
```

**逐行解释：**

- L121 `len > 4`：手册 §8.2 IICDATA 表只有 DATA0~DATA3 共 4 字节；这是 **写入长度的硬件上限**——这就是审查小节（§8.3）提到的 "test_i2c.c 只支持 1~4 字节"。
- L124–125：CTL0 是 `(addr<<1)|R/W`，写操作 `R/W=0`，所以这里直接是 `(addr<<1)&0xFF`，等价于 `(addr<<1)|0`。ADR0 是子地址（AT24C02 单字节子地址）。
- L128–131：4 字节小端拼字——`DATA0` 是 LSB，会最先发；`DATA3` 是 MSB。
- L135–137：动作序列 START0→CTL0→ADR0→[WDAT × DATA_CNT]→STOP。`DATA_CNT` 即 IICDATA 中有效字节数。
- L137 `(len & 0x7)`：注意 `DATA_CNT` 是 bit2:0，等于 0~7。但 `IICDATA` 只有 4 个 byte 字段，所以实际可用范围 1~4（被 L121 限制）。

### 6.4 硬件 IIC：读 N 字节 + 重复起始（test_i2c.c 第 152–189 行）

```c
152 static bool test_iic_read(u8 dev_addr7, u8 reg_addr,
153                            u8 *buf, u8 len, u32 timeout_us)
154 {
155     if (len == 0 || len > 4) return false;
156
157     // Phase 1: START + address(W) + sub-address (no STOP)
158     IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)             // CTL0 = address+W
159             | ((u32)reg_addr << 8);                     // ADR0 = sub-address
160
161     // Phase 2: repeated START + address(R)
162     // manual Sec.8.2 IICCMDA bit 31:24 = CTL1
163     IICCMDA |= (u32)(((dev_addr7 << 1) | 1u) & 0xFF) << 24;
164
165     IICDATA = 0;   // placeholder load
166
167     // Action sequence: START0 + CTL0 + ADR0 + START1 + CTL1 + RDAT + STOP
168     // Multi-byte read last byte gets NAK (manual Sec.8.2 IICCON1[12] = TXNAK_EN)
169     IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_ADR0_EN
170             | IIC_START1_EN | IIC_CTL1_EN
171             | IIC_RDAT_EN | IIC_STOP_EN
172             | IIC_TXNAK_EN              // last byte NAK (standard I2C)
173             | (len & 0x7);
174
175     IICCON0 |= IIC_KS;
...
182     u32 data_word = IICDATA;
183     for (u8 i = 0; i < len; i++) {
184         buf[i] = (u8)((data_word >> (i * 8)) & 0xFF);
185     }
...
189 }
```

**逐行解释：**

- L163 `IICCMDA |= ... << 24`：在已经填好 CTL0/ADR0 的基础上，再把 CTL1[31:24] 填入"地址+R"，这是 IIC 控制器用于重复起始 Sr 的第二地址——读到 FIFO 后立刻发 START1+CTL1 再进入读。
- L169–173：动作序列把 `START0+CTL0+ADR0`（写子地址）、`START1+CTL1`（重复起始 + 读地址）、`RDAT`（收 N byte）、`STOP` 串成一次事务。
- L172 `IIC_TXNAK_EN`：手册 §8.2 IICCON1 bit12——读最后一字节发 NAK，符合标准 I2C 总线协议。
- L182 `data_word`：读接收数据从 `IICDATA` 读出后，按 DATA0=LSB 小端拆出各字节。

### 6.5 软件 bit-bang：PAD 初始化与 GPIO 宏（test_i2c_gpio.c 第 23–42、166–177 行）

```c
24  #define SCL_OUT()       do { GPIOEDIR &= ~PE6_MASK; } while (0)  // CRITICAL!
25  #define SCL_HIGH()      do { SCL_OUT(); GPIOESET = PE6_MASK; } while (0)
26  #define SCL_LOW()       do { SCL_OUT(); GPIOECLR = PE6_MASK; } while (0)
27
28  // PE7 SDA: switch DIR for open-drain emulation
29  #define SDA_OUT()       do { GPIOEDIR &= ~PE7_MASK; } while (0)
30  #define SDA_IN()        do { GPIOEDIR |=  PE7_MASK; } while (0)
31  #define SDA_OUT_HIGH()  do { SDA_OUT(); GPIOESET = PE7_MASK; } while (0)
32  #define SDA_OUT_LOW()   do { SDA_OUT(); GPIOECLR = PE7_MASK; } while (0)
33  #define SDA_READ()      ((GPIOE & PE7_MASK) ? 1 : 0)
```

**关键点：**
- L24 `SCL_OUT()`：在 `SCL_HIGH/LOW` 之前先把 PE6 DIR 清 0（输出）。**这一行是之前 bug 的修复点**——原版只调 `GPIOESET=PE6_MASK`，但 PE6 DIR 如果是默认输入态（手册 §3.2 `DIR` 默认 0xFF=全输入），SET 操作对外无效。test_soft_i2c.md §3.8 详细记录了这个修复过程。
- L29–30 `SDA_OUT()/SDA_IN()`：通过切换 DIR 模拟开漏。
- L33 `SDA_READ()`：读 PE7 当前值，与 DIR 无关（手册 §3.2 "读为输入状态"）。

```c
166 static void i2c_gpio_init_pads(void)
167 {
168     GPIOEDE  |= PE6_7_MASK;       // digital enable
169     GPIOEFEN &= ~PE6_7_MASK;      // GPIO mode (NOT peripheral) — bit-bang 必须
170     GPIOEPU  |= PE6_7_MASK;       // 10K pull-up
171     GPIOEPD  &= ~PE6_7_MASK;
172     SDA_OUT_HIGH();
173     SCL_HIGH();                   // 内含 SCL_OUT()
174     delay_us(100);
175 }
```

**逐行解释：**
- L168 `DE=1`：数字 IO。
- L169 `FEN=0`：**关键**——bit-bang 必须让 PAD 由 GPIO 自身控制，不能交给任何外设。
- L170 `PU=1`：10K 上拉使能，对 SDA 来说是 I2C 规范必需（开漏 idle=高）。
- L172 `SDA_OUT_HIGH`：先把 PE7 DIR=输出，再 SET 输出高。
- L173 `SCL_HIGH`：同上但对 PE6。

### 6.6 软件 bit-bang：START / STOP / 字节收发（test_i2c_gpio.c 第 44–116 行）

```c
45  static void i2c_gpio_start(void)
46  {
47      SDA_OUT_HIGH();
48      SCL_HIGH();
49      I2C_DELAY();        // delay_us(5)
50      SDA_OUT_LOW();      // SDA falls while SCL high = START condition
51      I2C_DELAY();
52      SCL_LOW();          // prep clocking
53      I2C_DELAY();
54  }
```

```c
58  static void i2c_gpio_stop(void)
59  {
60      SDA_OUT_LOW();
61      I2C_DELAY();
62      SCL_HIGH();
63      I2C_DELAY();
64      SDA_OUT_HIGH();     // SDA rises while SCL high = STOP condition
65      I2C_DELAY();
66  }
```

```c
69  static bool i2c_gpio_write_byte(u8 data)
70  {
71      for (u8 i = 0; i < 8; i++) {
72          if (data & 0x80) SDA_OUT_HIGH();
73          else             SDA_OUT_LOW();
74          I2C_TSU();        // 1µs data setup
75          SCL_HIGH();
76          I2C_DELAY();      // 5µs SCL high
77          SCL_LOW();
78          I2C_DELAY();      // 5µs SCL low
79          data <<= 1;
80      }
81      // ACK slot: release SDA so slave can drive
82      SDA_IN();             // 切输入，开漏释放
83      I2C_TSU();
84      SCL_HIGH();
85      I2C_DELAY();
86      bool ack = (SDA_READ() == 0);
87      SCL_LOW();
88      I2C_DELAY();
89      return ack;
90  }
```

```c
94  static u8 i2c_gpio_read_byte(bool send_ack)
95  {
96      u8 val = 0;
97      SDA_IN();          // release SDA for slave
98      for (u8 i = 0; i < 8; i++) {
99          SCL_HIGH();
100         I2C_DELAY();
101         val = (val << 1) | SDA_READ();
102         SCL_LOW();
103         I2C_DELAY();
104     }
105     SDA_OUT();          // 主机要发 ACK/NAK，把 SDA 切回输出
106     if (send_ack) SDA_OUT_LOW();   // ACK = 拉低
107     else          SDA_OUT_HIGH();  // NAK = 拉高
108     I2C_TSU();
109     SCL_HIGH();
110     I2C_DELAY();
111     SCL_LOW();
112     I2C_DELAY();
113     SDA_OUT_HIGH();     // 释放准备下一位
114     return val;
115 }
```

**时序分析：**

| 阶段 | T_su_dat | T_high | T_low | 单 bit 时长 |
|:---|:---|:---|:---|:---|
| bit-bang | 1 µs | 5 µs | 5 µs | **≈ 11 µs**（即 SCL ≈ 91 kHz） |
| 硬件 IIC | 由芯片决定 | 由芯片决定 | 由芯片决定 | LA 实测 ≈ 7.94 µs ≈ **125 kHz** |

### 6.7 软件 bit-bang：高层事务（test_i2c_gpio.c 第 121–163 行）

```c
121 static bool i2c_gpio_probe_addr(u8 dev_addr7, bool is_read)
122 {
123     i2c_gpio_start();
124     u8 ctl = (dev_addr7 << 1) | (is_read ? 1u : 0u);
125     bool ack = i2c_gpio_write_byte(ctl);
126     i2c_gpio_stop();
127     return ack;
128 }
```

```c
132 static bool i2c_gpio_write(u8 dev_addr7, u8 reg_addr, const u8 *data, u8 len)
133 {
134     if (len == 0 || len > 4) return false;
135     i2c_gpio_start();
136     if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) { i2c_gpio_stop(); return false; }
137     if (!i2c_gpio_write_byte(reg_addr))               { i2c_gpio_stop(); return false; }
138     for (u8 i = 0; i < len; i++) {
139         if (!i2c_gpio_write_byte(data[i]))           { i2c_gpio_stop(); return false; }
140     }
141     i2c_gpio_stop();
142     return true;
143 }
```

```c
148 static bool i2c_gpio_read(u8 dev_addr7, u8 reg_addr, u8 *buf, u8 len)
149 {
150     if (len == 0 || len > 4) return false;
151     i2c_gpio_start();
152     if (!i2c_gpio_write_byte((dev_addr7 << 1) | 0u)) { i2c_gpio_stop(); return false; }
153     if (!i2c_gpio_write_byte(reg_addr))               { i2c_gpio_stop(); return false; }
154     i2c_gpio_start();                          // Sr = repeated START
155     if (!i2c_gpio_write_byte((dev_addr7 << 1) | 1u)) { i2c_gpio_stop(); return false; }
156     for (u8 i = 0; i < len - 1; i++) {
157         buf[i] = i2c_gpio_read_byte(true);     // ACK (more bytes coming)
158     }
159     buf[len - 1] = i2c_gpio_read_byte(false);  // NAK (last byte)
160     i2c_gpio_stop();
161     return true;
162 }
```

**协议要点：**
- L154 重复起始 Sr：读事务的标准模式——先发"地址+W + 子地址"建立读指针，再发 START（不发 STOP）切换到"地址+R"。
- L157–159 中间字节 ACK、最后一字节 NAK：与硬件版的 `IIC_TXNAK_EN` 等价（test_i2c.c L172）。

### 6.8 错误处理：任何从机 NAK 都立即 STOP

两个 bit-bang 读/写函数都遵循同一模式——只要某个 `write_byte` 返回 false（NAK）就 `i2c_gpio_stop()` 然后退出，**不发完整事务不进入死锁状态**，这是 I2C 总线的标准恢复机制。

### 6.9 LA 测试：连续 burst（test_i2c_la.c 第 66–88 行 / test_i2c_gpio_la.c 第 36–72 行）

```c
66  static bool la_probe(u32 timeout_us)
67  {
68      IICCON0 |= IIC_CLR_ALL;
69      IICCMDA = (u8)((AT24C02_ADDR << 1) | 0);  // 0xA0
70      IICCON1 = IIC_START0_EN | IIC_CTL0_EN | IIC_STOP_EN | 0;
71      IICCON0 |= IIC_KS;
72      u32 t0 = TMR2CNT;
73      while (!(IICCON0 & IIC_DONE)) {
74          if ((u32)(TMR2CNT - t0) > timeout_us) {
75              IICCON0 |= IIC_CLR_DONE;
76              return false;
77          }
78      }
79      IICCON0 |= IIC_CLR_DONE;
80      return true;
81 }
```

**关键差异**（与 test_i2c.c 相比）：
- L68 **每次前先 CLR_ALL**：保证上一次事务残留（包括 NAK）不会影响本次——因为 LA 测试时 AT24C02 可能断开，从机不响应会 NAK，残留 NAK 状态需要清。
- L73 **1 ms 超时**：AT24C02 断开 → 总线一直等 ACK → 必须超时退出，否则卡死。

---

## 7. 测试步骤与预期现象

### 7.1 接线方式

#### 7.1.1 AT24C02 功能测试（硬件 IIC 与 bit-bang 共用）

```
BT892X                  AT24C02 模块
------                  -----------
PE6  ──────────────────  SCL
PE7  ──────────────────  SDA
3.3V ──────────────────  VCC
GND  ──────────────────  GND
                        A0/A1/A2 ── GND (7-bit 地址 = 0x50)
```

> **注**：本工程 PE6/PE7 已启用内部 10K 上拉，**外部上拉可选**；若总线上拉不够（总线过长/从机多）建议在 SCL/SDA 各加 4.7K 外拉到 3.3V。

#### 7.1.2 逻辑分析仪时序测试（test_i2c_la.c / test_i2c_gpio_la.c）

```
LA / 示波器             BT892X
-----------             -------
CH1 (SCL) ─────────────  PE6
CH2 (SDA) ─────────────  PE7
GND   ─────────────────  GND
AT24C02 模块：可接可不接（断开 VCC 也行）
```

#### 7.1.3 测试矩阵

| 测试 | 主程序 | AT24C02 | LA | 串口输出 |
|:---|:---|:---|:---|:---|
| **硬件 IIC 功能** | `test_i2c_run()` | 必接 | 不必 | 必接（1.5 Mbps @ PB3） |
| **硬件 IIC 时序** | `test_i2c_la_run()` | 可不接 | 必接 | 必接（看提示） |
| **软件 bit-bang 功能** | `test_i2c_gpio_run()` | 必接 | 不必 | 必接 |
| **软件 bit-bang 时序** | `test_i2c_gpio_la_run()` | 可不接 | 必接 | 必接 |

### 7.2 main.c 需开启的 `TEST_*_EN` 宏

main.c 第 68–71 行目前都是注释（默认关闭）。需要在编译时打开：

```c
// 4 个 I2C 测试入口，任选其一打开：
#define TEST_I2C_EN         1   // -> test_i2c_run()        (硬件 IIC + AT24C02)
#define TEST_I2C_LA_EN      1   // -> test_i2c_la_run()     (硬件 IIC 时序)
#define TEST_I2C_GPIO_EN    1   // -> test_i2c_gpio_run()   (软件 bit-bang + AT24C02)
#define TEST_I2C_GPIO_LA_EN 1   // -> test_i2c_gpio_la_run()(软件 bit-bang 时序)
```

**互斥约束（main.c 第 56–71 行注释）：**
- 4 个 `TEST_I2C_*_EN` 之间：因为都使用 PE6/PE7，**同一时刻只能打开一个**。
- `TEST_I2C_*_EN` 与 `TEST_SPI_*_EN`：也共用 PE6/PE7（SPI 用 PE6=CLK / PE7=MOSI / PE5=MISO / PE4=CS），互斥。
- 与 UART2 / ADKEY / TIMER：无引脚交集，可同开。

> 实际工程操作：注释其它、打开一个，单独烧写验证。

### 7.3 串口输出预期（直接引用 test_hard_i2c.md §2.6 与 test_soft_i2c.md §3.7 实测）

| 阶段 | 串口日志（`[TEST]` 前缀由 `TEST_LOG` 宏自动加） | 期望判定 |
|:---|:---|:---|
| 启动 | `AT24C02 hardware I2C test` / `AT24C02 GPIO bit-bang I2C test`<br>`Pins: PE6=SCL, PE7=SDA...` | — |
| **Test 1** | `Probe 0x50: ACK` | AT24C02 在线 |
| **Test 2**（scan 0x08~0x77） | `Found device at 0x50`<br>`Scan done: 1 device(s) found` | 应当只有 0x50 应答 |
| **Test 3**（write 0x55@0x00） | `Write: ACK` | 写成功 |
| **Test 4**（read 0x55@0x00） | `Read: ACK, data=0x55`<br>`WRITE-READ PASS` | 数据回读一致 |
| **Test 5**（4 字节 pattern） | `Write 0xDE 0xAD 0xBE 0xEF: ACK`<br>`Read: ACK, data=0xDE 0xAD 0xBE 0xEF`<br>`PATTERN PASS` | 4 字节读写一致 |

**实测（user-verified，2026-07-17）：硬件 IIC 与软件 bit-bang 路径都全 5 项 PASS**（docs/test_hard_i2c.md 第 222–238 行 / docs/test_soft_i2c.md 第 261–275 行）。

LA 时序测试（test_i2c_la.c / test_i2c_gpio_la.c）额外打印：

```
[TEST] [Initial state]
[TEST]   CLKGAT1 = 0x.....
[TEST]   CLKCON1 = 0x.....
[TEST]   CLKCON2 = 0x.....
[TEST]   IICCON0 = 0x.....
[TEST] [Burst 1] bursting 60 transactions...
[TEST]   >> Trigger LA now on PE6 rising edge <<
[TEST] [Burst 2 (verify)] bursting 60 transactions...
[TEST]   Measure one SCL pulse on PE6 in LA.
```

### 7.4 逻辑分析仪波形预期

#### 7.4.1 硬件 IIC（test_i2c_la.c）

LA 在 PE6 上触发（falling edge），捕获 1 个完整事务 `START + 8 bit 地址 + ACK + STOP`：

```
                 ┌─T_scl─┐ ┌─T_scl─┐ ┌─T_scl─┐ ... ┌─T_scl─┐
PE6 (SCL):  ____─┤_LOW_HI_├─┤_LOW_HI_├─┤_LOW_HI_├─...─┤_LOW_HI_├─...______
PE7 (SDA):  _____↓___________________________↑_________↓_↑_____↑_________↑___
            START   D7=1      D6=0   D5=1     ...  D0=0  ↑  ACK   STOP
                                   0xA0 = 1010_0000     (从机拉低)  (SCL高时SDA升)
```

| 字段 | 期望 | 实测 |
|:---|:---|:---|
| SCL 高电平时间 | 由 `POSDIV=19` 决定：约 8 µs（手册 §8.2 `SCL = IICK / 20`） | LA 测得 ≈ 7.94 µs |
| SCL 单 bit 周期 | ≈ 8 µs × 2 = 16 µs 折算后约 125 kHz | **实测 SCL ≈ 125 kHz** |
| 实际 IICK | 由 LA 反推：`1 / 7.94µs × 20 ≈ 2.52 MHz` | docs/test_hard_i2c.md §3.6 |

#### 7.4.2 软件 bit-bang（test_i2c_gpio_la.c）

```
                 ┌─11µs──┐ ┌─11µs──┐ ┌─11µs──┐ ┌─11µs──┐ ... ┌─11µs──┐
PE6 (SCL):  ____─┤LOW_HI_├─┤LOW_HI_├─┤LOW_HI_├─┤LOW_HI_├─...─┤LOW_HI_├─...______
PE7 (SDA):  _____↓___________________________↑_________↓_↑_____↑_________↑___
            START   D7=1      D6=0   D5=1           ↑  ACK   STOP
                                0xA0 = 1010_0000
```

| 字段 | 期望 | 实测 |
|:---|:---|:---|
| SCL 单 bit 周期 | 1 + 5 + 5 = 11 µs | **实测 SCL ≈ 83–91 kHz**（docs/test_soft_i2c.md §4.6） |
| START/STOP 标志 | SCL 高时 SDA 下降/上升 | 通过 |

> 注：bit-bang 的 11 µs/周期 ≈ 91 kHz，硬件版的 7.94 µs/周期 ≈ 125 kHz——硬件 IIC 实际工作更接近手册目标的 100 kHz Fast-mode 上限。两者在 Standard-mode 100 kHz 规范下都 **足够慢** 让 AT24C02 正常 ACK。

### 7.5 关键实测数据汇总（直接引用 test_*.md）

| 项 | 硬件 IIC | 软件 bit-bang |
|:---|:---|:---|
| 探测 0x50 | ACK（test_hard_i2c.md §2.6） | ACK（test_soft_i2c.md §3.7） |
| 0x08–0x77 扫描 | 仅 0x50 ACK | 仅 0x50 ACK |
| 单字节 0x55 写读 | 一致 PASS | 一致 PASS |
| 4 字节 0xDEADBEEF 写读 | 一致 PASS | 一致 PASS |
| 实测 SCL 频率 | **≈ 125 kHz**（IICK ≈ 2.5 MHz） | **≈ 83 kHz**（11 µs/bit） |

---

## 8. 审查小节（对照手册与引脚定义）

### 8.1 与 BT892X_UserManual_Driver.md §8 一致性核对

| 代码行为 | 手册依据 | 一致性 | 备注 |
|:---|:---|:---|:---|
| `CLKGAT2 \|= BIT(0)` 开 IIC 时钟门 | 用户提供的 CLKGAT 表（手册未列位定义） | ✅ 满足手册 §8.3 step 1 描述（"配置 IO 映射"前的最低要求） | 手册本体缺 CLKGAT 表 |
| `FUNCMCON2[24:27] = 5` | 手册 §3.3 FUNCMCON2 表（手册只列到 bit19:16）| ✅ 引脚表推得 | pinfunction.md §3 第 90 行明确给出 `FUNCMCON2[24:27]=IIC` |
| `GPIOEDE/PU/PD` 配置 PE6/PE7 | §3.2 + §8.3 | ✅ | — |
| `GPIOEFEN \|= PE6_7_MASK`（硬件 IIC） | §3.2（=1 交给外设）| ✅ | — |
| `IICCON0[9:4] = 19` | §8.2 表 (`POSDIV` 字段) | ✅ | 注释 `=19÷20` 与手册 `(N+1)` 描述一致 |
| `IICCON0[3:2] = 0`（HOLDCNT）| §8.2 表（"0=1 周期"）| ✅ | — |
| `IICCON0[0] = IIC_EN` | §8.2 表 | ✅ | — |
| `IICCON0[27] = CLR_ALL` | §8.2 表 | ✅ | — |
| `IICCON0[28] = KS` | §8.2 表（写 1 启动） | ✅ | — |
| `IICCON0[29] = CLR_DONE` | §8.2 表（写 1 清 DONE） | ✅ | — |
| `IICCON0[30] = ACKSTATUS`（读）| §8.2 表 | ✅ | — |
| `IICCON0[31] = DONE`（读）| §8.2 表 | ✅ | — |
| `IICCON1[3]`=`START0_EN` / `[4]`=`CTL0_EN` / `[5]`=`ADR0_EN` / `[7]`=`START1_EN` / `[8]`=`CTL1_EN` / `[9]`=`RDAT_EN` / `[10]`=`WDAT_EN` / `[11]`=`STOP_EN` / `[12]`=`TXNAK_EN` | §8.2 IICCON1 表（第 570–584 行）| ✅ 10 个 bit 定义都正确 | — |
| `IICCON1[2:0] = DATA_CNT` ≤ 4 | §8.2 + §8.1 特性"最大 4 字节" | ✅ | — |
| `IICCMDA[7:0]`=`CTL0` / `[15:8]`=`ADR0` / `[31:24]`=`CTL1` | §8.2 IICCMDA 表 | ✅ | — |
| `IICDATA[7:0]`=`DATA0`（最先收发）| §8.2 IICDATA 表 + §8.1 "DATA0 最先" | ✅ | — |
| 重复起始 Sr：START0+CTL0+ADR0+START1+CTL1 | §8.3 step 6 + Sr 是 I2C 标准协议 | ✅ | — |
| 单 byte → 多 byte data 的小端打包 | §8.2 IICDATA 字段定义 | ✅ | — |
| `delay_us(100)` 后再写 `KS` | 手册未明示 | ⚠️ 经验值 | 见 §8.3 |
| `INTEN=0` 不用中断，纯轮询 | §8.2 `INTEN` bit1（默认 0） | ✅ | 简化设计 |

### 8.2 与 bt892x_pinfunction.md 一致性

| 代码行为 | 引脚表依据 | 一致性 | 备注 |
|:---|:---|:---|:---|
| `FUNCMCON2[24:27] = 5` → G5 | §4.3 PE6 行：`IIC_CLK-G5/G6`；PE7 行：`IIC_DAT-G5`；§3 第 90 行 IICMAP 在 FUNCMCON2[24:27]；§8.5 IIC 信号 | ✅ | **G5 是唯一同时承载 PE6 SCL+PE7 SDA 的 Group** |
| PE6 = SCL, PE7 = SDA | §4.3 PE6/PE7 引脚行 IIC 列 | ✅ | — |
| bit-bang 路径 `GPIOEFEN &= ~PE6_7_MASK` | §3.2 "0 = GPIO 模式" | ✅ | 让 PAD 由 GPIO 控制 |
| hardware 路径 `GPIOEFEN \|= PE6_7_MASK` | §3.2 "1 = 用作功能 IO" | ✅ | 让 PAD 由 IIC 控制器接管 |
| 10K 上拉 / 关下拉 | §3.2 PU=1 启用 10K | ✅ | — |

### 8.3 发现的疑点 / 不一致（**如实记录，不要求改代码**）

下面列出的疑点都来自 docs/test_*.md 与 sfr.h 之间的不一致、或手册本身的盲点，目的是为后续维护者提供风险记录。

#### 疑点 A：手册 §8.2 没有 preclkdiv 寄存器定义与默认值

- **现象**：手册 §8.2 给出公式 `SCL = source_clk / ((preclkdiv+1) * (posdiv+1))`，但 `preclkdiv` 没有对应的寄存器字段说明，默认值也无从查证。`test_i2c.c` 直接使用默认值没有任何显式配置。
- **影响**：依赖 un-documented 的默认值。实测 IICK ≈ 2.5 MHz（test_hard_i2c.md §3.6），但手册 §8.1 列出的源是 "RC2M 或 XOSC26M"——不知道默认走哪一个、也不清楚会不会在不同芯片/不同 VDD 下漂移。
- **建议**：待芯片 FAE 给出 preclkdiv 寄存器位定义后再决定是否显式写入。
- **本工程做法**：先记录 LA 实测 SCL 频率作为后续回归 baseline。

#### 疑点 B：`docs/test_hard_i2c.md §4 表` 中 `CLKGAT2` 地址写为 `0x3E4`，与 `sfr.h` 第 101 行的实际地址 `0x0F8` 不一致

- **现象**：测试报告表 (`SFR0_BASE+0x3E*4`) 列出的十六进制 `0x3E4` 数学上不等于 `0xF8`，但 `SFR0_BASE+0x3E*4` 表达式确实等于 `0xF8`——推测是 test_hard_i2c.md 作者笔误。
- **影响**：仅文档不一致，不影响代码；维护者若按文档 grep `0x3E4` 会找不到。
- **建议**：维护时统一改用 `sfr.h` 实际地址 `0x0F8`。

#### 疑点 C：test_i2c.c 的写长度上限是 4，是硬件 `IICDATA` 字段限制的硬上限

- **现象**：`test_iic_write` 在 L121 检查 `len > 4` 直接 return false；read 同。
- **影响**：大于 4 字节的 AT24C02 页写（AT24C02 页大小实际是 8 byte）需要循环多次；本工程代码未实现多页写。
- **建议**：未来要做 >4 字节需自己分页（页内 8 字节、写周期 5 ms）。

#### 疑点 D：硬件 IIC + 软件 bit-bang 共用 PE6/PE7 但无运行时分时复用机制

- **现象**：两路径都用 `FEN` 反向 (`|=` vs `&=~`) 切换 PAD 控制权，但二者运行时序互斥完全靠 `main.c` 中 `TEST_*_EN` 宏约定（任一时刻只打开一个）。
- **影响**：误开两个 `TEST_I2C_*_EN` 不会报错，但 PAD 被两边争用，硬件 IIC 控制器驱动时会与 GPIO 写冲突，行为未定义。
- **建议**：明确写一个 `i2c_gpio_release()` / `i2c_hard_release()` 静态守卫；或一个 `i2c_lock()` 信号量；当前以 make 时隔离为短期可行解。

#### 疑点 E：bit-bang 路径有过 bug（已修）

- **现象**：test_i2c_gpio.c L24 注释 `// CRITICAL: was missing!`，并由 test_soft_i2c.md §3.8 记录：原版 `SCL_HIGH()` 只写 `GPIOESET=PE6_MASK` 而未先 `GPIOEDIR&=~PE6_MASK`，在 PE6 默认 DIR=输入下无效。
- **修复**：`SCL_OUT()` 宏显式设 DIR=0；`SCL_HIGH()/LOW()` 都先 `SCL_OUT()`。
- **影响**：当前代码已修复；维护者改动时务必保留 `SCL_OUT()` 调用。

#### 疑点 F：test_i2c_gpio_la.c 头文件注释的引脚定义错了

- **现象**：`test_i2c_gpio_la.h` 第 5 行注释写 "Pins: PE5=SCL, PE6=SDA"，但实际 `.c` 第 11 行 `PE6_MASK` 用于 SCL，`PE7_MASK` 用于 SDA。
- **影响**：仅文档头注释笔误，不影响代码逻辑；维护者读头文件会迷惑。
- **建议**：维护时把头文件注释改成 PE6=SCL, PE7=SDA。

#### 疑点 G：手册 §8.1 列 "RC2M 或 XOSC26M"，但没指明 IIC 控制器默认走哪个

- **现象**：手册 §8.1 "支持异步时钟源（RC2M 或 XOSC26M）" 是二选一能力描述；sfr.h / 头文件也没有 `IIC_CLKSEL` 之类的字段（不在 `IICCON0/1` 中）。
- **影响**：无法显式切换时钟源。实测 IICK ≈ 2.5 MHz 介于 RC2M（标称 2 MHz）与 XOSC26M（标称 26 MHz）之间——最可能是 RC2M + 内部某个分频，但缺文档无法确认。
- **建议**：联系厂商 FAE 索取 preclkdiv 与时钟源选择位的完整定义。

#### 疑点 H：硬件 IIC 与软件 bit-bang 四个测试文件大量重复宏/位定义

- **现象**：四个 `.c` 文件中重复定义 `IIC_EN/KS/DONE/...`、`PE6_7_MASK`、`AT24C02_ADDR=0x50` 等；test_i2c_gpio.c 与 test_i2c_gpio_la.c 的宏 `SCL_OUT/SDA_IN` 完全相同。
- **影响**：维护者修改一处需要同步改其它三处。代码体积约 150 行重复。
- **建议**：后续抽出 `test_i2c_common.h`（存放 `IIC_EN` 等宏）+ `test_i2c_gpio_common.c/.h`（存放 `SDA_IN/SDA_OUT/SCL_HIGH/LOW`）。当前为简化测试代码没做并不影响功能。

### 8.4 一致性核对结论

✅ **总体一致性良好**：寄存器位定义、引脚 Group、上拉/方向配置、`POSDIV/HOLDCNT/IIC_EN`、动作使能位、DONE/ACKSTATUS 语义、Sr 重复起始、小端数据打包——**全部与 BT892X_UserManual_Driver.md §8 + bt892x_pinfunction.md §4.3 §8.5 一致**。

⚠️ **手册缺失**：§8.2 没有 `preclkdiv` 寄存器位定义、§8.1 没有 IIC 时钟源选择位的说明、CLKGAT 表完全缺失。这些空白是后续维护者最可能踩坑的点。

⚠️ **文档小瑕疵**：
- `docs/test_hard_i2c.md §4 表` 的 `CLKGAT2` 地址 `0x3E4` 与 `sfr.h` 第 101 行的 `0x0F8` 不一致（笔误）。
- `test_i2c_gpio_la.h` 第 5 行注释的引脚定义（PE5/PE6）与 `.c` 实际（PE6/PE7）不一致（笔误）。
- 4 个测试文件重复定义宏约 150 行；未来重构可抽出 `test_i2c_common.h`。

---

> **文档结束**
> 
> 本文档基于：
> - 手册 `docs/BT892X_UserManual_Driver.md`（v0.0.1，2021-03-30）
> - 引脚表 `docs/bt892x_pinfunction.md`（2023-08-30）
> - 实测报告 `docs/test_hard_i2c.md`、`docs/test_soft_i2c.md`（2026-07-17）
> - 源码 `smart_mini/test/test_i2c.c/.h`、`test_i2c_la.c/.h`、`test_i2c_gpio.c/.h`、`test_i2c_gpio_la.c/.h`、`main.c`
> - 寄存器定义 `smart_mini/header/sfr.h`（IIC 寄存器在第 359–362 行，GPIOE 在第 450–462 行，FUNCMCON2 在第 46 行，CLKGAT2 在第 101 行）