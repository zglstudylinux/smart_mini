# BT892X GPIO 测试报告

> **测试日期**：2026-07-17
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §3 GPIO 管理](../BT892X_UserManual_Driver.md)
> - [bt892x_pinfunction.md §4.3 PORTE / §4.2 PORTB](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**GPIO 数字输出**（PE4/PE5/PE6/PE7 + PB1/PB2 引脚翻转诊断）
> **测试结果**：✅ 通过（PE4/PE5/PE6/PE7/PB1/PB2 均观测到方波）

---

## 0. 文档结构

本测试用纯 GPIO 寄存器操作（`GPIOxDE/FEN/DIR/SET/CLR`）把指定引脚配置为普通数字输出，并以 1Hz 方波翻转，用逻辑分析仪/示波器验证每个引脚在开发板上物理可达。

| 程序 | 路径 | 用途 | 验证方式 |
|---|---|---|---|
| **test_gpio.c** | `smart_mini/test/test_gpio.c` | PE/PB 引脚 1Hz 方波翻转诊断 | 串口打印 + 逻辑分析仪/示波器观测波形 |

> 本测试**只使用 GPIO 通用控制寄存器**，不涉及任何外设映射（`FEN` 全程保持关闭 = 用作 GPIO）。

---

## 1. 手册源码引用

每一条寄存器操作都标注了手册出处。

### 1.1 GPIO 通用控制寄存器（手册 [§3.2](../BT892X_UserManual_Driver.md) 第 141-157 行，"以 Port A 为例"，PB/PE 各端口寄存器完全一致）

```
寄存器       | 名称           | 关键说明
------------|----------------|--------------------------------------------------
GPIOx       | 数据寄存器      | 读为输入状态，写为输出状态（bit 与引脚一一对应）
GPIOxSET    | 置位寄存器      | 写 1 置位对应位 → 输出高电平
GPIOxCLR    | 清除寄存器      | 写 1 清除对应位 → 输出低电平
GPIOxDIR    | 方向寄存器      | 0: 输出；1: 输入（默认 0xFF 全输入）
GPIOxDE     | 数字功能使能    | 0: 模拟 IO；1: 数字 IO（默认 0xFF）
GPIOxFEN    | 功能映射使能    | 0: 用作 GPIO；1: 用作功能 IO（默认 0xFF）
GPIOxDRV    | 输出驱动选择    | 0: 8mA；1: 32mA（默认 0x0）
GPIOxPU/PD  | 10KΩ 上/下拉    | 输入模式下有效
```

> **要点（手册 §3.2）**：`x` 为端口字母（A/B/E/F/G）。要把引脚当普通数字输出用，必须 `DE=1`（数字）、`FEN=0`（GPIO 而非功能）、`DIR=0`（输出）；置位/清零分别写 `SET`/`CLR`（写 1 有效，写 0 无动作）。

### 1.2 引脚功能定义（[bt892x_pinfunction.md §4.3 PORTE](../bt892x_pinfunction.md) 第 121-129 行 / §4.2 PORTB 第 115-116 行）

| 引脚 | 手册标注 | IO 类型 | 说明（手册第 121-129 / 115-116 行 + 备注 329/332） |
|---|---|---|---|
| **PE4/SPIDIN** | SD PG | TYPE1 | 支持 0.3K/10K/200K 上下拉，可用作 GPIO |
| **PE5** | — | TYPE1 | 同上 |
| **PE6** | — | TYPE1 | 同上 |
| **PE7/ADKEY** | ADKEY | TYPE1 | 同上 |
| **PB1/WK2** | 唤醒源 WK2 | TYPE1_WKO | 兼作唤醒源，本测试不进 sleep 无影响（备注 332） |
| **PB2/WK3** | 唤醒源 WK3 | TYPE1_WKO | 兼作唤醒源，同上 |

> **跳过的引脚（有手册依据）**：
> - **PE0**：手册 §4.3 第 125 行标注 `高压PIN MUTE / TYPE4`，仅 10K 固定上下拉（备注 329），涉及高压，测试跳过。
> - **PB0**：本次未测（可用但非本轮目标）。
> - **PB3/PB4/PB5**：PB3=UART0 debug TX（本工程 `main.c` 用于 printf）、PB4=USB DM、PB5=WKO 10S Reset 主唤醒源（备注 332），均不可占用。

### 1.3 GPIO 寄存器地址（[header/sfr.h](../smart_mini/header/sfr.h) 第 450-462 行 PE / 第 436-448 行 PB，`SFR6_BASE = 0x600`）

```
GPIOESET = SFR6_BASE+0x20*4 = 0x680      GPIOBSET = SFR6_BASE+0x10*4 = 0x640
GPIOECLR = SFR6_BASE+0x21*4 = 0x684      GPIOBCLR = SFR6_BASE+0x11*4 = 0x644
GPIOE    = SFR6_BASE+0x22*4 = 0x688      GPIOB    = SFR6_BASE+0x12*4 = 0x648
GPIOEDIR = SFR6_BASE+0x23*4 = 0x68C      GPIOBDIR = SFR6_BASE+0x13*4 = 0x64C
GPIOEDE  = SFR6_BASE+0x24*4 = 0x690      GPIOBDE  = SFR6_BASE+0x14*4 = 0x650
GPIOEFEN = SFR6_BASE+0x25*4 = 0x694      GPIOBFEN = SFR6_BASE+0x15*4 = 0x654
```

---

## 2. 测试程序：test_gpio.c

### 2.1 引脚分配

| 引脚 | 角色 | 手册依据 |
|---|---|---|
| PE4 | GPIO 输出（1Hz 方波） | [pinfunction §4.3](../bt892x_pinfunction.md) 第 126 行 |
| PE5 | GPIO 输出（1Hz 方波） | [pinfunction §4.3](../bt892x_pinfunction.md) 第 127 行 |
| PE6 | GPIO 输出（1Hz 方波） | [pinfunction §4.3](../bt892x_pinfunction.md) 第 128 行 |
| PE7 | GPIO 输出（1Hz 方波） | [pinfunction §4.3](../bt892x_pinfunction.md) 第 129 行 |
| PB1 | GPIO 输出（1Hz 方波） | [pinfunction §4.2](../bt892x_pinfunction.md) 第 115 行 |
| PB2 | GPIO 输出（1Hz 方波） | [pinfunction §4.2](../bt892x_pinfunction.md) 第 116 行 |

### 2.2 寄存器配置原理

`TEST_PIN_TOGGLE(PORT, pin)` 宏对每个引脚执行以下序列，每行都注明手册来源：

```c
// ---- 配置为普通数字输出（手册 §3.2 GPIO 通用控制寄存器）----
GPIOxDE  |=  BIT(pin);   // §3.2: DE=1 → 数字 IO
GPIOxFEN &= ~BIT(pin);   // §3.2: FEN=0 → 用作 GPIO（不映射到外设功能）
GPIOxDIR &= ~BIT(pin);   // §3.2: DIR=0 → 输出
GPIOxCLR  =  BIT(pin);   // §3.2: 写 CLR=1 → 初始输出低

// ---- 1Hz 方波循环（持续 2 秒）----
while ((u32)(TMR2CNT - t0) < duration_ms * 1000) {
    GPIOxSET = BIT(pin);  // §3.2: 写 SET=1 → 输出高
    delay_ms(500);        // main.c 的 TMR2 1µs tick（见 test_timer.md）
    GPIOxCLR = BIT(pin);  // §3.2: 写 CLR=1 → 输出低
    delay_ms(500);
}

// ---- 恢复默认（手册 §3.2 默认值 DIR=1/FEN=1/DE=0 方向）----
GPIOxDIR |=  BIT(pin);
GPIOxFEN |=  BIT(pin);
GPIOxDE  &= ~BIT(pin);
```

> **两层宏技巧**：`GPIO##PORT##DE` 中 `PORT` 参与 `##` 拼接时不会先展开，故 `test_common.h` 用 `TEST_PIN_TOGGLE` → 内层 helper 两层宏先展开 `PORT`（E/B）再拼接（详见 `test_common.h` 注释）。

### 2.3 周期控制

高电平 500ms + 低电平 500ms = **1 秒完整周期（1Hz）**，每个引脚翻转 **2 秒**（约 2 个完整周期）。计时用 `main.c` 已初始化的 **TMR2（1µs/tick，来源 `x26m_div_clk = 1MHz`）**，通过 `delay_ms()` 与 `TMR2CNT` 实现（时钟配置见 `main.c` 第 188-191 行，`TICK_1MS=1000` 见 `header/include.h`）。

### 2.4 测试流程

| 顺序 | 引脚 | 动作 | 期望 |
|---|---|---|---|
| 1 | PE4 | 1Hz 方波 2s | 波形 0V/3.3V 交替 |
| 2 | PE5 | 1Hz 方波 2s | 同上 |
| 3 | PE6 | 1Hz 方波 2s | 同上 |
| 4 | PE7 | 1Hz 方波 2s | 同上 |
| 5 | PB1 | 1Hz 方波 2s | 同上 |
| 6 | PB2 | 1Hz 方波 2s | 同上 |

### 2.5 实测结果

串口（UART0 = PB3 @ 1.5Mbps）输出（节选）：

```
[TEST] ========================================
[TEST] PE/PB GPIO diagnostic test
[TEST] Connect logic analyzer probes to PE4/5/6/7 and PB1/2
[TEST] Each pin will toggle 1Hz for 2 seconds
[TEST] *** PE port ***
[TEST] >>> Testing PE4: toggle 1Hz for 2000 ms
[TEST] >>> Probe PE4 with logic analyzer now!
[TEST] <<< PE4 done (2 toggles, pin left LOW)
... (PE5 / PE6 / PE7)
[TEST] *** PB port (only PB1/PB2) ***
[TEST] >>> Testing PB1: toggle 1Hz for 2000 ms
[TEST] >>> Testing PB2: toggle 1Hz for 2000 ms
[TEST] ========================================
[TEST] All tested pins done.
```

**结论**：✅ 用户实测确认 —— PE4/PE5/PE6/PE7/PB1/PB2 六个引脚均按预期输出 1Hz 方波，串口打印同步，GPIO 寄存器读写功能正常。

---

## 3. 完整寄存器表

| 寄存器 | 地址（sfr.h） | 配置 | 手册依据 |
|---|---|---|---|
| `GPIOEDE` / `GPIOBDE` | 0x690 / 0x650 | `\|= BIT(pin)` 数字 IO 使能 | §3.2 第 155 行 |
| `GPIOEFEN` / `GPIOBFEN` | 0x694 / 0x654 | `&= ~BIT(pin)` 用作 GPIO | §3.2 第 156 行 |
| `GPIOEDIR` / `GPIOBDIR` | 0x68C / 0x64C | `&= ~BIT(pin)` 设为输出 | §3.2 第 148 行 |
| `GPIOESET` / `GPIOBSET` | 0x680 / 0x640 | `= BIT(pin)` 输出高 | §3.2 第 146 行 |
| `GPIOECLR` / `GPIOBCLR` | 0x684 / 0x644 | `= BIT(pin)` 输出低 | §3.2 第 147 行 |
| `TMR2CNT` | 0xF0 (`SFR0_BASE+0x3C*4`) | 只读，1µs tick 计时 | main.c timer2_init（见 test_timer.md） |

---

## 4. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| 编译报错 `undeclared GPIOxxxDIR` | 宏参数 `##` 拼接前未展开 | 用双层宏先展开 PORT（`test_common.h` 已处理） |
| 编译报错 `undefined reference to test_gpio_run` | `app.cbp` 未登记 test_gpio.c | 在 `<Unit>` 列表加入 |
| 下载后某引脚无波形 | 该引脚物理不可达或被复用 | 对照 pinfunction §4.3；确认非跳过引脚 |
| 引脚一直高 | 方向配成输入 | 确认 `GPIOxDIR &= ~BIT(pin)`（§3.2 DIR=0 输出） |
| 引脚电平不翻转（恒定） | `FEN` 未清 → 引脚被外设占用 | 确认 `GPIOxFEN &= ~BIT(pin)`（§3.2 FEN=0） |
| 串口无 [TEST] 打印 | `TEST_GPIO_EN` 未开 | 确认 `main.c` `#define TEST_GPIO_EN 1` |
| 周期远大于 1s | 系统时钟未切到 24MHz | 确认 `set_sys_clk(SYS_24M)`（main.c 默认已配） |

---

## 5. 工程意义

- **测试文件**：`test_gpio.c` 一个文件覆盖 PE/PB 多引脚翻转诊断，用于确认开发板上哪些引脚物理可达。
- **纯 GPIO 路径**：全程 `FEN=0`，不涉及任何外设映射，是后续 TIMER/UART/SPI/I2C 引脚复用配置的基础。
- **手册依据**：每一条寄存器操作都对应 [BT892X_UserManual_Driver.md §3.2](../BT892X_UserManual_Driver.md) 或 [bt892x_pinfunction.md §4.3](../bt892x_pinfunction.md)。

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_gpio.c` | GPIO 引脚翻转诊断实现 |
| `smart_mini/test/test_gpio.h` | 头文件（`void test_gpio_run(void)`） |
| `smart_mini/test/test_common.h` | 共用 `TEST_LOG` + `TEST_GPIO_*` 宏（双层宏解决 `##` 展开） |
| `smart_mini/main.c` | 入口（`#define TEST_GPIO_EN 1` 开关 + `test_gpio_run()` 调用） |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 .c 文件） |
| `smart_mini/header/sfr.h` | SFR 寄存器宏定义（第 421-462 行 GPIO A/B/E 组） |
| `docs/BT892X_UserManual_Driver.md` | 芯片寄存器手册（§3 GPIO，第 134-157 行） |
| `docs/bt892x_pinfunction.md` | 引脚功能定义（§4.3 PORTE 第 121-129 行、§4.2 PORTB 第 115-116 行） |
