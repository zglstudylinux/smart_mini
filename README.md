# smart_mini — BT892X 外设学习测试工程

本工程基于中科蓝讯 **BT892X**（32 位 RISC-V SoC），通过**寄存器级编程**逐步测试 6 大外设：GPIO、Timer、UART、SPI、I2C、ADC（ADKEY）。

## 工程结构

```
minimax/
├── docs/                                  # 参考手册 + 测试报告 + 外设技术说明 + 学习文档
│   ├── BT892X_UserManual_Driver.md        # 寄存器驱动手册
│   ├── bt892x_pinfunction.md              # 引脚功能定义
│   ├── bt892x_pinfunction-20230830.xlsx   # 引脚定义表 Excel 版
│   ├── TWS  DEV V2.2.pdf                  # 开发板原理图
│   ├── bt892x_usermanual.pdf              # 芯片用户手册 PDF
│   ├── "SARADC_CTL (逐次逼近型 ADC) 数据手册摘要.md"
│   ├── plan.md                            # 整体学习计划
│   ├── periph_gpio.md                     # ★ GPIO 技术说明（寄存器/引脚/原理/测试/审查）
│   ├── periph_timer.md                    # ★ Timer 技术说明（TMR0/1/2 + TMR3 PWM）
│   ├── periph_uart.md                     # ★ UART 技术说明（软/硬/console + ringbuf）
│   ├── periph_spi.md                      # ★ SPI 技术说明（软/硬/W25Q64/IT/DMA）
│   ├── periph_i2c.md                      # ★ I2C 技术说明（硬件 IIC + 软件 bit-bang）
│   ├── periph_key.md                      # ★ ADKEY 技术说明（PB5/ADC12 + 5 阶段按键状态机）
│   ├── learn_adc.md                       # ★ ADC 学习笔记（结合 ADKEY 测试示例）
│   ├── test_gpio.md                       # GPIO 测试报告 ✅
│   ├── test_timer.md                      # Timer 测试报告 ✅
│   ├── test_timer_pwm.md                  # Timer PWM 测试报告 ✅
│   ├── test_uart.md                       # UART2 测试报告 ✅
│   ├── test_uart_hal_refactor.md          # UART HAL 重构记录
│   ├── test_uart_ringbuf.md               # ★ UART ringbuf 设计文档
│   ├── test_spi.md                        # SPI 测试报告 ✅（5 个 phase 全过）
│   ├── test_spi_hal_refactor.md           # SPI HAL 重构记录
│   ├── test_spi_w25q64.md                 # ★ W25Q64 Flash 全套测试报告
│   ├── test_hard_i2c.md                   # 硬件 I2C 测试报告 ✅
│   ├── test_soft_i2c.md                   # 软件 I2C 测试报告 ✅
│   ├── test_i2c_at24c02.md                # ★ AT24C02 测试报告
│   ├── test_i2c_hal_refactor.md           # ★ I2C HAL 重构记录
│   └── test_adkey.md                      # ★ ADKEY 完整测试报告（5 阶段）
└── smart_mini/                            # CodeBlocks 工程根
    ├── app.cbp                            # CodeBlocks 工程文件
    ├── main.c                             # 主入口（系统初始化 + 测试调度）
    ├── interrupt.c                        # 中断处理
    ├── reset.S                            # 启动文件
    ├── ram.ld                             # 链接脚本
    ├── header/                            # 头文件（sfr.h 寄存器定义、include.h 等）
    ├── test/                              # 测试代码 + UART/SPI/I2C HAL
    │   ├── uart_hal.c/.h                  # UART HAL（软+硬）
    │   ├── spi_hal.c/.h                   # SPI HAL（软+硬）
    │   ├── i2c_hal.c/.h                   # I2C HAL（软+硬 + AT24C02 业务封装）
    │   ├── test_common.h
    │   ├── test_gpio.c/.h
    │   ├── test_timer.c/.h
    │   ├── test_timer_pwm.c/.h
    │   ├── test_uart.c/.h                 # 4 入口：loop/send/recv/console + soft
    │   ├── test_spi_loop.c/.h             # 跳线回环
    │   ├── test_spi_wave.c/.h             # LA 抓波形
    │   ├── test_spi_w25q64.c/.h           # W25Q64 Flash 全套
    │   ├── test_spi_soft_asm.c/.h         # bit-bang C vs ASM 速度对比
    │   ├── test_spi_timing.c/.h           # polling/IT/DMA 三模式时间对比
    │   ├── test_i2c.c/.h                  # 硬件 IIC + AT24C02
    │   ├── test_i2c_gpio.c/.h             # 软件 bit-bang + AT24C02
    │   ├── test_i2c_la.c/.h               # 硬件 IIC LA 时序测试
    │   ├── test_i2c_gpio_la.c/.h          # 软件 I2C LA 时序测试
    │   └── test_adkey.c/.h                # ADKEY 5 阶段（RAW/MAP/DEBOUNCE/LONG/HOLD）
    └── Output/                            # 构建产物（按提交惯例入仓）
```

## 外设技术说明文档

6 份新整理的**寄存器级技术文档**（`docs/periph_*.md`），每份包含：寄存器逐个说明（含 `sfr.h` 行号 + 位域 + 手册章节）→ 引脚定义与复用 → 初始化原理 → 初始化步骤 → 代码详解 → 测试步骤与预期现象 → 审查小节（对照手册/引脚的一致性核对 + 疑点记录）。

| 外设 | 文档 | 涉及寄存器 | 关键引脚 |
|---|---|---|---|
| GPIO | [docs/periph_gpio.md](docs/periph_gpio.md) | GPIOxDE/FEN/DIR/SET/CLR 等 13 个 | PE4/5/6/7、PB1/2 |
| TIM  | [docs/periph_timer.md](docs/periph_timer.md) | TMR0/1/2CON/CPND/CNT/PR + TMR3CON/CPND/PR/DUTY0/1/2 | PB0/1/2（PWM） |
| UART | [docs/periph_uart.md](docs/periph_uart.md) | UART2CON/CPND/BAUD/DATA | PB2(TX2-G2)/PB1(RX2-G2) |
| SPI  | [docs/periph_spi.md](docs/periph_spi.md) | SPI1CON/BUF/BAUD/CPND/DMACNT/DMAADR | PE4(CS)/PE5(MISO)/PE6(CLK)/PE7(MOSI) |
| I2C  | [docs/periph_i2c.md](docs/periph_i2c.md) | IICCON0/IICCON1/IICCMDA/IICDATA | PE6(SCL)/PE7(SDA) — G5 |
| ADKEY | [docs/periph_key.md](docs/periph_key.md) | SADCCON/SADCCH/SADCBAUD/SADCDAT12/CLKCON0/CLKGAT0 | PB5(WKO/ADC12) |

## 测试报告 / 重构记录 / 学习文档

| 类型 | 文档 | 说明 |
|---|---|---|
| 学习 | [docs/learn_adc.md](docs/learn_adc.md) | ADC 零基础教程（结合 ADKEY 测试）|
| 重构 | [docs/test_i2c_hal_refactor.md](docs/test_i2c_hal_refactor.md) | I2C HAL 浅重构记录 |
| 重构 | [docs/test_spi_hal_refactor.md](docs/test_spi_hal_refactor.md) | SPI HAL 浅重构记录 |
| 重构 | [docs/test_uart_hal_refactor.md](docs/test_uart_hal_refactor.md) | UART HAL 浅重构记录 |
| 设计 | [docs/test_uart_ringbuf.md](docs/test_uart_ringbuf.md) | UART ringbuf 设计文档 |
| 测试 | [docs/test_spi_w25q64.md](docs/test_spi_w25q64.md) | W25Q64 软+硬 18 个实验 |
| 测试 | [docs/test_i2c_at24c02.md](docs/test_i2c_at24c02.md) | AT24C02 5 个功能测试 |

## HAL 设计模式

3 个通信外设（UART/SPI/I2C）都有相同的 HAL 设计：

```
test/
├── xxx_hal.h              # HAL 接口（底层原语 + 中层 transaction + 业务封装）
├── xxx_hal.c              # HAL 实现（软 bit-bang + 硬件控制器 双实现）
└── test_xxx.c             # 测试入口（只调 API，不写寄存器）
```

软/硬 API **同名同形**（template-style）：

| 外设 | 硬件 API | 软件 API |
|---|---|---|
| UART | `uart_hal_hw_*` | `uart_hal_soft_*` |
| SPI  | `spi_hal_hw_*`  | `spi_hal_soft_*`  |
| I2C  | `i2c_hal_hw_*` / `at24c02_hw_*` | `i2c_hal_soft_*` / `at24c02_soft_*` |

测试代码切换硬件↔软件**只需改前缀**，不掺寄存器细节。

## 构建工具链

```
CodeBlocks IDE
  ↓ Build
riscv32-gcc 编译 .c/.S → .o
  ↓ Link
riscv32-ld -Tram.ld → app.rv32 (ELF)
  ↓ postbuild.bat
riscv32-elf-objcopy → app.bin
xmaker → app.xm → app.dcf (可烧录固件)
  ↓ Downloader 工具
写入 BT892X 芯片
```

## 开发流程

1. 用 **CodeBlocks** 打开 `smart_mini/app.cbp`
2. 编辑 `smart_mini/test/test_xxx.c` 或 `smart_mini/main.c`
3. 在 `main.c` 中**打开一个** `TEST_xxx_EN` 宏（一次只开一个）
4. **Build** → 生成 `Output/bin/app.dcf`
5. 用 **Downloader** 工具把 `app.dcf` 烧录到开发板
6. 通过 **UART0 (PB3)** 串口查看 `printf` 输出（默认 1.5 Mbps 8N1）
7. 用 **万用表** / **逻辑分析仪** 验证引脚电平和时序

## 测试模块

每个测试独立文件，入口函数 `void test_xxx_run(void)`：

| 模块 | 文件 | 引脚 | 状态 |
|---|---|---|---|
| GPIO | `test/test_gpio.c` | PE4/5/6/7、PB1/2 | ✅ 通过 |
| Timer (定时) | `test/test_timer.c` | TMR1（内部） | ✅ 通过 |
| Timer (PWM) | `test/test_timer_pwm.c` | PB0/1/2（PWM0/1/2-T3-G1） | ✅ 通过 |
| UART (硬件 UART2) | `test/test_uart.c` + `test/uart_hal.c` | PB2(TX2-G2)/PB1(RX2-G2) | ✅ 通过（loop / send / recv / console）|
| UART (软件 bit-bang) | `test/uart_hal.c` (soft) | PB2/PB1 | ✅ 通过（9600 8N1 回环）|
| SPI (硬件 SPI1) | `test/test_spi_loop.c` + `test/spi_hal.c` | PE4(CS)/PE6(CLK)/PE7(MOSI)/PE5(MISO) — G4 | ✅ 通过（loop / wave / timing 三模式）|
| SPI (软件 bit-bang) | `test/spi_hal.c` (soft) | PE4/PE6/PE7/PE5 | ✅ 通过（C vs ASM 速度对比）|
| SPI (W25Q64 Flash) | `test/test_spi_w25q64.c` | 同上 | ✅ 通过（JEDEC=0xEF4017，软+硬 18 个实验）|
| SPI (时间/性能) | `test/test_spi_timing.c` | 同上 | ✅ 通过（Polling / IT / DMA 时间对比）|
| I2C (硬件 IIC) | `test/test_i2c.c` + `i2c_hal.c` | PE6(SCL)/PE7(SDA) — G5 | ✅ 通过（AT24C02 全 5 子测试）|
| I2C (软件 bit-bang) | `test/test_i2c_gpio.c` + `i2c_hal.c` | PE6(SCL)/PE7(SDA) | ✅ 通过（AT24C02 全 5 子测试）|
| I2C (LA 时序) | `test/test_i2c_la.c` / `test_i2c_gpio_la.c` | PE6/PE7 | ✅ 通过（实测 SCL 周期，硬件 1.2 MHz / 软件 83 kHz）|
| AD 按键 (原始值) | `test/test_adkey.c` (`TEST_ADKEY_RAW_EN`) | PB5(WKO/ADC12) | ✅ 通过（双上拉稳定值：PLAY=24 / PREV=115 / NEXT=120 / NONE=122）|
| AD 按键 (按键映射) | `test/test_adkey.c` (`TEST_ADKEY_MAP_EN`) | 同上 | ✅ 通过（4 状态 + 121 死区）|
| AD 按键 (25ms 消抖) | `test/test_adkey.c` (`TEST_ADKEY_DEBOUNCE_EN`) | 同上 | ✅ 通过（SHORT/SHORT_UP 消息）|
| AD 按键 (700ms 长按) | `test/test_adkey.c` (`TEST_ADKEY_DEBOUNCE_EN` 中验证 LONG) | 同上 | ✅ 通过（LONG/LONG_UP 消息）|
| AD 按键 (200ms HOLD) | `test/test_adkey.c` (`TEST_ADKEY_HOLD_EN`) | 同上 | ✅ 通过（SHORT → LONG → 每 200ms HOLD → LONG_UP）|

详细计划见 [docs/plan.md](docs/plan.md)。

**跳过说明**：
- **UART1（PA6/PA7）** 因 PA 端口 0-7 物理损坏已弃用，工程改用 **UART2 (PB1/PB2)** 完成全部测试
- **SPI0** 因引脚冲突跳过，改用 **SPI1 (PE4-7 G4)** + W25Q64 Flash 验证全套

## 关键约束

- **PB3 / PB4 / PB5 / PG1~PG5** 是 USB 下载 / 程序存储 / 唤醒源，**严禁任何测试代码触碰**
- **TMR0 / TMR2** 已被 main.c 占用（1ms 中断 / 1µs tick），**Timer 测试只能用 TMR1**
- **PB1 / PB2** 是 UART2 与 TMR3 PWM 共用引脚，`TEST_UART_*_EN` 与 `TEST_TIMER_PWM_EN` **不能同时开**
- **PE4 / PE5 / PE6 / PE7** 是 5 个 SPI 测试的共用引脚，**一次只开一个**
- **PE6 / PE7** 是 4 个 I2C 测试的共用引脚，**一次只开一个**
- 所有测试代码基于 `header/sfr.h` 中的寄存器宏直接操作
- 测试设备：万用表（量电压）、逻辑分析仪（抓波形）、USB-TTL 串口（看 printf）

## 开关切换速查

`main.c` 中所有测试开关集中在 L48-78，按"一次只开一个"原则切换：

```c
// ---- GPIO ----
// #define TEST_GPIO_EN    1

// ---- Timer ----
// #define TEST_TIMER_EN   1
// #define TEST_TIMER_PWM_EN  1

// ---- UART：5 个测试入口（PB2/PB1 共用，一次只开一个）----
// #define TEST_UART_EN            1   // 硬件 UART2 回环（跳线 PB2<->PB1）
// #define TEST_UART_SEND_EN       1   // 硬件 UART2 持续发送
// #define TEST_UART_RECV_EN       1   // 硬件 UART2 持续接收
// #define TEST_UART_CONSOLE_EN    1   // 硬件 UART2 收发回显 + printf 路由
// #define TEST_UART_SOFT_EN       1   // 软件 bit-bang UART 回环

// ---- SPI：5 个测试（PE4-7 共用，一次只开一个）----
// #define TEST_SPI_LOOP_EN       1
// #define TEST_SPI_WAVE_EN       1
// #define TEST_SPI_W25Q64_EN     1
// #define TEST_SPI_SOFT_ASM_EN   1
// #define TEST_SPI_TIMING_EN     1

// ---- I2C：4 个测试（PE6/PE7 共用，一次只开一个）----
// #define TEST_I2C_EN            1
// #define TEST_I2C_LA_EN         1
// #define TEST_I2C_GPIO_EN       1
// #define TEST_I2C_GPIO_LA_EN    1

// ---- ADKEY：5 阶段（一次只开一个）----
// #define TEST_ADKEY_RAW_EN      1
// #define TEST_ADKEY_MAP_EN      1
// #define TEST_ADKEY_DEBOUNCE_EN 1
// #define TEST_ADKEY_LONG_EN     1
 #define TEST_ADKEY_HOLD_EN     1   // 阶段五：完整状态机
```

## Git 工作流

- **主分支**：`main`（2026-07-23 从 `smart_mini_minimax` 合并而来）
- **历史分支**：`smart_mini_minimax`（开发用，已合并，可保留或删除）
- **远程**：`https://github.com/zglstudylinux/smart_mini.git`
- **提交节奏**：**每个测试 / 每个重构 / 每个文档 = 1 次 commit + 1 次 push**
- **提交信息规范**：
  - `test(xxx): add xxx driver test and report` — 新测试
  - `refactor(xxx): HAL shallow refactor + docs` — HAL 重构
  - `docs(xxx): add xxx doc/test report/learning note` — 纯文档
  - `fix(xxx): xxx bug + xxx fix` — 修复