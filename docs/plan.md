# BT892X 嵌入式学习测试计划

> 本文档描述在 smart_mini CodeBlocks 工程中**逐个测试 BT892X 五大外设**的整体规划。
> 测试顺序：GPIO → Timer → UART1 → SPI (bit-bang) → I2C (bit-bang)
> 每个外设**独立实现、独立验证、独立写文档、独立 push 到 GitHub**。

---

## Context（背景与目标）

用户是嵌入式新手，需要在 `smart_mini/` CodeBlocks 工程中**逐个测试 BT892X 的 GPIO / Timer / UART / SPI / I2C 五个外设**，通过寄存器操作（不用 SDK 高级封装），用万用表和逻辑分析仪验证，**每个测试独立完成、独立写文档、独立 push 到 GitHub**。目的是系统掌握芯片外设寄存器操作，建立从手册→代码→验证→文档的完整开发流程。

---

## 关键约束（红线）

| 引脚/资源 | 默认用途 | 规则 |
|---|---|---|
| **PB3** | UART0 debug TX（G3，单线模式） | **严禁任何测试触碰** — 动了 printf 就废了 |
| **PB4** | USB DM | 严禁触碰 — 影响 USB 下载 |
| **PB5** | WKO（10s 复位唤醒） | 严禁触碰 |
| **PG1~PG5** | SPI-Flash / MCP（程序存储） | 严禁触碰 — 破坏芯片启动 |
| **TMR2** | 1 us tick（main.c 已占） | 不许改 TMR2 配置 |
| **TMR0** | 1 ms 中断（timer0_init 已占） | 不许改 TMR0 配置 |
| **main.c 初始化流程** | WDT/USB/SD/UART0/CLK | 不许修改，只在 `main()` 末尾追加测试调用 |

---

## 已确认的关键决策

1. **GPIO 测试引脚**：仅 PE4（一个干净引脚，先从最简单的开始）
2. **UART 测试**：用 **UART1**，Group G2 = **PA3(TX) + PA4(RX)**，不影响 PB3 debug
3. **SPI 测试**：先用 **GPIO bit-bang**（避开硬件 SPI0 引脚冲突），后续再补硬件 SPI0 测试
4. **I2C 测试**：先用 **GPIO bit-bang**，后续再补硬件 I2C 测试
5. **Timer 测试**：使用 **TMR1**（TMR0/TMR2 已被占用），保持 tmr_inc = 1 MHz 默认源
6. **提交节奏**：每个测试 = 1 次 commit + 1 次 push
7. **测试代码组织**：`smart_mini/test/` 目录下独立 C 文件，`main.c` 通过 `extern` 调用
8. **Git 仓库**：直接在 `d:\Code\smart_mini\minimax` 初始化 git，远程为 `git@github.com:zglstudylinux/smart_mini.git`，分支 `smart_mini_minimax`

---

## 文件清单

### 新建文件

| 路径 | 用途 |
|---|---|
| `d:\Code\smart_mini\minimax\.gitignore` | Git 忽略 `Output/obj/`, `Output/bin/`, `*.dcf`, `app.layout` 等 |
| `d:\Code\smart_mini\minimax\README.md` | 项目说明 + 构建流程 + 测试目录说明 |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_common.h` | 测试共用宏（LOG 打印、GPIO 操作宏） |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_gpio.c/h` | GPIO 测试 |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_timer.c/h` | Timer 测试 |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_uart.c/h` | UART 测试 |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_spi.c/h` | SPI bit-bang 测试 |
| `d:\Code\smart_mini\minimax\smart_mini\test\test_i2c.c/h` | I2C bit-bang 测试 |
| `d:\Code\smart_mini\minimax\docs\test_gpio.md` | GPIO 测试报告 |
| `d:\Code\smart_mini\minimax\docs\test_timer.md` | Timer 测试报告 |
| `d:\Code\smart_mini\minimax\docs\test_uart.md` | UART 测试报告 |
| `d:\Code\smart_mini\minimax\docs\test_spi.md` | SPI 测试报告 |
| `d:\Code\smart_mini\minimax\docs\test_i2c.md` | I2C 测试报告 |

### 修改文件

| 路径 | 修改内容 |
|---|---|
| `d:\Code\smart_mini\minimax\smart_mini\app.cbp` | 在 `<Unit>` 列表插入 10 个 test/ 文件（5 个 .c + 5 个 .h） |
| `d:\Code\smart_mini\minimax\smart_mini\main.c` | `main()` 末尾 `while(1);` 之前追加 5 个 `test_xxx_run()` 调用 + extern 声明 |

### 不动的文件（只读）

- `d:\Code\smart_mini\minimax\smart_mini\header\` 所有头文件
- `d:\Code\smart_mini\minimax\smart_mini\interrupt.c`
- `d:\Code\smart_mini\minimax\smart_mini\reset.S`
- `d:\Code\smart_mini\minimax\smart_mini\ram.ld`
- `d:\Code\smart_mini\minimax\docs\BT892X_UserManual_Driver.md`、`bt892x_pinfunction.md` 等已有手册

---

## 实施步骤

### 阶段 0：初始化 Git + 上传初始工程（一次性）

1. 在 `d:\Code\smart_mini\minimax` 创建 `.gitignore`（排除构建产物）
2. 创建 `README.md`（项目简介 + 构建流程 + 测试目录说明）
3. `git init` → `git checkout -b smart_mini_minimax`
4. `git remote add origin git@github.com:zglstudylinux/smart_mini.git`
5. `git add` + `git commit -m "init: import BT892X smart_mini project skeleton"`
6. `git push -u origin smart_mini_minimax`

### 阶段 1：GPIO 测试（PE4 输出闪烁）

**关键寄存器**（基于 sfr.h）：
- `GPIOEDE` (0x690) bit 4 — 数字 IO 使能
- `GPIOEFEN` (0x694) bit 4 — 功能映射关闭（=0 用 GPIO）
- `GPIOEDIR` (0x68C) bit 4 — 0=输出，1=输入
- `GPIOESET` (0x680) / `GPIOECLR` (0x684) bit 4 — 写 1 置位/清零

**测试步骤**：
1. 配置 PE4 为 GPIO 输出
2. 循环翻转（500ms 高 / 500ms 低），用万用表量 PE4 电压
3. printf 每次翻转状态

**用户验证**：万用表读 PE4 在 3.3V 和 0V 间周期切换，周期 ~1s

**完成后**：写 `docs/test_gpio.md` + commit + push

### 阶段 2：Timer 测试（TMR1 精度测量）

**关键寄存器**：
- `TMR1CON` (0xD4) bit0=TMREN, bit7=TIE, bit9=TPND
- `TMR1CNT` (0xD8) 32 位计数
- `TMR1PR` (0xDC) 周期寄存器
- `IRQ_TMR1_VECTOR`（查 int.h）

**时钟源**（main.c 已配置）：`tmr_inc = x26m_div_clk = 1 MHz`

**测试步骤**：
1. **轮询模式**：配置 TMR1PR = 999（1ms）/9999（10ms）/999999（1s），用 TMR2CNT 测量实际经过时间，串口打印误差
2. **中断模式**：注册 `test_timer1_isr` 到 IRQ_TMR1_VECTOR，1ms 中断，统计 1000 次中断耗时

**用户验证**：串口打印测量时间与期望值误差 < ±5%

**完成后**：写 `docs/test_timer.md` + commit + push

### 阶段 3：UART1 测试（PA3/PA4 双线通信）

**关键寄存器**：
- `FUNCMCON0` bits [27:24]=UT1TXMAP=0x2, [31:28]=UT1RXMAP=0x2（G2）
- `GPIOADE` bit3,4 / `GPIOAFEN` bit3,4 — 数字 + 功能
- `GPIOAPU` bit3 — RX 上拉
- `GPIOADIR` bit4=0 (TX出), bit3=1 (RX入)
- `UART1CON` (0xC0) bit0=UTEN, bit7=RXEN, bit8=TXPND, bit9=RXPND
- `UART1BAUD` (0xC8) 高16位=RX分频，低16位=TX分频
- `UART1DATA` (0xCC) 数据
- `UART1CPND` (0xC4) 写 1 清中断

**波特率计算（24 MHz）**：`BAUD = 24000000 / 115200 - 1 = 207`

**测试步骤**：
1. **TX-only**：连续发送 "UART1\r\n"，逻辑分析仪抓 PA3 波形
2. **Loopback**：短接 PA3↔PA4，发送 "Hello UART1"，比较接收数据
3. **波特率切换**：测 9600 / 115200 两种波特率

**用户验证**：
- 逻辑分析仪看 PA3 的 UART 波形（起始位+8位数据+停止位）
- 短接 PA3↔PA4 后串口打印 "RX: Hello UART1"

**完成后**：写 `docs/test_uart.md` + commit + push

### 阶段 4：SPI bit-bang 测试

**bit-bang 引脚分配**（完全空闲）：
- CS  = PF0
- CLK = PF1
- MOSI = PE5
- MISO = PE6

**关键寄存器**：同 GPIO 测试（GPIOEDE / GPIOEFEN / GPIOEDIR / GPIOF*）

**测试步骤**：
1. 配置 4 个 GPIO（CS/CLK/MOSI 输出，MISO 输入）
2. 实现 `spi_sw_transfer_byte(u8 tx)` — 标准 Mode 0 时序（CLK idle low, 上升沿采样）
3. **内部 Loopback**：短接 MOSI↔MISO（PE5↔PE6），发送 0x55/0xAA/0x5A，验证收发一致
4. **时序观测**：逻辑分析仪连 PF1（CLK）和 PE5（MOSI），观察波形

**用户验证**：
- 串口打印 "TX=0x55 RX=0x55 OK" 三次
- 逻辑分析仪看到标准 SPI 波形

**完成后**：写 `docs/test_spi.md` + commit + push

### 阶段 5：I2C bit-bang 测试

**bit-bang 引脚分配**：
- SCL = PE6
- SDA = PE7

**关键寄存器**：同 GPIO 测试

**测试步骤**：
1. 配置 PE6(SCL) 输出、PE7(SDA) 输出+输入切换（开漏模拟）
2. 实现 `iic_start()`, `iic_stop()`, `iic_send_byte()`, `iic_recv_byte(ack)`
3. **地址扫描**：扫描 0x08~0x77 的 7 位地址，串口打印应答的地址
4. **无外设验证**：逻辑分析仪看 PE6/PE7 波形确认 START/STOP/ACK 时序正确

**用户验证**：
- 无外设时串口打印 "No I2C device found"
- 逻辑分析仪看到正确的 I2C START + 7 位地址 + ACK 时序

**完成后**：写 `docs/test_i2c.md` + commit + push

---

## 验证流程（每个测试）

1. **代码层面**：在 VSCode 编辑 test/test_xxx.c，保存
2. **编译**：用 CodeBlocks 打开 `app.cbp` → Build → 生成 `Output/bin/app.dcf`
3. **下载**：用 Downloader 工具把 `app.dcf` 写入开发板
4. **观察**：
   - GPIO：万用表量电压
   - Timer：看串口打印的时间测量值
   - UART：逻辑分析仪抓波形 + 串口打印接收数据
   - SPI：逻辑分析仪抓 CLK/MOSI 波形 + 串口打印收发对比
   - I2C：逻辑分析仪抓 SCL/SDA 波形 + 串口打印扫描结果
5. **确认现象正确** → 写 `docs/test_xxx.md` → commit → push → 下一个

---

## 文档模板（每个 `docs/test_xxx.md`）

```markdown
# <模块> 测试报告

- 测试日期：YYYY-MM-DD
- 关联 commit：<hash>

## 1. 测试目标
## 2. 测试原理（关键寄存器表 + 手册章节引用）
## 3. 引脚分配表
## 4. 测试设备（万用表 / 逻辑分析仪型号）
## 5. 测试步骤
## 6. 预期结果
## 7. 实测结果（粘贴串口打印 / 测量数据）
## 8. 寄存器配置表（地址 / 配置值 / 说明）
## 9. 失败排查思路
## 10. 后续建议
```