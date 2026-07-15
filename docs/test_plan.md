# BT892X 外设逐步测试计划

> **日期**: 2026-07-15  
> **芯片**: BT892X (中科蓝讯 32-bit RISC)  
> **仓库**: git@github.com:zglstudylinux/smart_mini.git  
> **分支**: smart_mini_copilot

---

## 概述

在 `smart_mini/test/` 下为 GPIO → 定时器(PWM) → UART1 → SPI1 → I2C 每个外设编写基于寄存器操作的独立测试代码。每次测试通过后生成文档并提交 GitHub。首先将初始工程推送到 `smart_mini_copilot` 分支。

**测试工具**: 万用表、逻辑分析仪  
**编译方式**: Code::Blocks 编译生成 `.dcf` → Downloader 下载运行  
**验证方式**: printf 串口输出 + 仪器测量

---

## Phase 0: GitHub 仓库初始化

### 目标
将当前初始工程推送到 GitHub，建立开发基线。

### 步骤
1. 在工程根目录初始化 git 仓库
2. 创建 `.gitignore` 文件（排除编译产物：`*.dcf`, `*.xm`, `*.lst`, `map.txt`，保留源码）
3. 创建分支 `smart_mini_copilot`
4. 添加远程仓库 `git@github.com:zglstudylinux/smart_mini.git`
5. 首次提交并推送

### 产物
- `.gitignore`
- GitHub 仓库 `smart_mini_copilot` 分支

---

## Phase 1: GPIO 测试

### 目标
验证 GPIO 输出（高低电平翻转）和 GPIO 输入（读取引脚状态），通过万用表和跳线验证。

### 测试文件
- `smart_mini/test/test.h` — 测试公共头文件
- `smart_mini/test/gpio_test.c` — GPIO 测试代码

### 测试内容

#### 1.1 GPIO 输出测试（万用表验证）
- **引脚**: PE4（空闲无冲突，TYPE1，默认 8mA 驱动）
- **寄存器配置**（参考手册 §3.2）:
  - `GPIOEDIR &= ~BIT(4)` — bit4=0，输出模式（DIR: 0=输出, 1=输入）
  - `GPIOEDE |= BIT(4)` — bit4=1，数字 IO 使能
  - `GPIOEFEN &= ~BIT(4)` — bit4=0，GPIO 模式（非功能 IO）
- **测试方法**: 循环翻转 `GPIOE` bit4（高/低交替），每次持续 1 秒
  - 万用表直流电压档测 PE4 对 GND：高电平 ≈3.3V，低电平 ≈0V
  - printf 输出当前状态到串口

#### 1.2 GPIO 输入测试（跳线验证）
- **输入引脚**: PF0（空闲无冲突）
- **寄存器配置**:
  - `GPIOFDIR |= BIT(0)` — bit0=1，输入模式
  - `GPIOFDE |= BIT(0)` — bit0=1，数字 IO 使能
  - `GPIOFFEN &= ~BIT(0)` — bit0=0，GPIO 模式
  - `GPIOFPU |= BIT(0)` — 10K 上拉使能
- **测试方法**:
  1. 不接跳线时读取 PF0，上拉应为高电平（1）
  2. PF0 接地时读取应为低电平（0）
  3. 跳线连接 PE4（输出）→ PF0（输入），输出高/低时验证读取值一致

### 文档
- `docs/gpio_test_report.md`

---

## Phase 2: 定时器 PWM 测试

### 目标
使用 Timer3 输出 PWM 波形，通过逻辑分析仪验证周期和占空比。

### 测试文件
- `smart_mini/test/timer_pwm_test.c`

### 测试内容

#### 2.1 单路 PWM 输出
- **定时器**: Timer3（不影响 Timer0 系统中断和 Timer2 delay）
- **时钟源**: tmr_inc = 1MHz（与现有代码 `CLKCON0[24]` 一致）
- **PWM 映射**: TMR3MAP = G1（`FUNCMCON2[11:8] = 0x1`）
  - PWM0 → PB0, PWM1 → PB1, PWM2 → PB2
- **寄存器配置**（参考手册 §4.3）:
  - `TMR3CNT = 0`
  - `TMR3PR = 1000 - 1` — 周期 1ms（1MHz / 1000 = 1KHz）
  - `TMR3DUTY0 = 500 - 1` — 占空比 50%（低电平 500µs，高电平 500µs）
  - `TMR3CON = BIT(9) | BIT(2) | BIT(0)` — PWM0EN + INCSEL=00(system clock) + TMREN

  > **注意**: PWM 占空比公式：低电平长度 = DUTY + 1，高电平长度 = PR - DUTY  
  > PWM0EN=1 时，PWM0 输出到映射引脚

- **验证**: 逻辑分析仪测 PB0，确认 1KHz 方波，占空比 50%

#### 2.2 多路 PWM 同时输出
- PWM0=PB0 (25%), PWM1=PB1 (50%), PWM2=PB2 (75%)
- 三路同时启用，逻辑分析仪同时捕获

### 文档
- `docs/timer_pwm_test_report.md`

---

## Phase 3: UART 测试

### 目标
使用 UART1 进行自发自收（回环）测试，验证 UART 通信。

### 测试文件
- `smart_mini/test/uart_test.c`

### 测试内容

#### 3.1 UART1 回环测试
- **UART1 映射**: G2（`FUNCMCON0[27:24]=0x2`, `FUNCMCON0[31:28]=0x2`）
  - TX → PA4, RX → PA3
- **GPIO 配置**:
  - PA4: `GPIOAFEN|=BIT(4)`, `GPIOADE|=BIT(4)`, `GPIOADIR&=~BIT(4)`, `GPIOAPU|=BIT(4)`
  - PA3: `GPIOAFEN|=BIT(3)`, `GPIOADE|=BIT(3)`, `GPIOADIR|=BIT(3)`, `GPIOAPU|=BIT(3)`
- **UART 配置**（参考手册 §6）:
  - 波特率 115200: `UART1BAUD = (207<<16)|207`（24MHz/115200-1≈207）
  - `UART1CON = BIT(7)|BIT(2)|BIT(0)` — RXEN + RXIE + UTEN
- **测试方法**:
  1. 用跳线短接 PA4(TX) 和 PA3(RX)
  2. 发送 0x00~0xFF 全部 256 个字节
  3. 接收并比对，printf 输出结果

#### 3.2 逻辑分析仪验证
- 去掉跳线，逻辑分析仪接 PA4，捕获 UART 帧
- 验证波特率 115200、起始位/数据位/停止位正确

### 文档
- `docs/uart_test_report.md`

---

## Phase 4: SPI 测试

### 目标
使用 SPI1 主机模式发送数据，逻辑分析仪验证时序。

### 测试文件
- `smart_mini/test/spi_test.c`

### 测试内容

#### 4.1 SPI1 主机发送（逻辑分析仪）
- **SPI1 映射**: G4（`FUNCMCON1[15:12]=0x4`）
  - CLK → PE6, DI(MISO) → PE5, DO(MOSI) → PE7
- **GPIO 配置**: PE5/PE6/PE7 设为功能 IO 模式
- **SPI 配置**（参考手册 §7）:
  - 模式 0: CLKIDS=0（空闲低）, SMPS=0（下降沿输出）, SPIOSS=0
  - 3 线模式: BUSMODE=00
  - 主机模式: SPISM=0
  - 波特率 1MHz: `SPI1BAUD = 23`（24MHz/24=1MHz）
  - `SPI1CON = BIT(0)` — SPIEN
- **测试方法**: 依次发送 0x55, 0xAA, 0x00, 0xFF
  - 逻辑分析仪接 PE6(CLK) 和 PE7(MOSI) 观察时序

#### 4.2 SPI1 回环测试
- 跳线短接 PE7(MOSI) 和 PE5(MISO)
- 发送数据并读取回环数据验证

### 文档
- `docs/spi_test_report.md`

---

## Phase 5: I2C 测试

### 目标
使用 IIC 主机模式发送 START/地址/数据/STOP，逻辑分析仪验证时序。

### 测试文件
- `smart_mini/test/i2c_test.c`

### 测试内容

#### 5.1 IIC 主机发送（逻辑分析仪）
- **IIC 映射**: G5（`FUNCMCON2[27:24]=0x5`）
  - SCL → PE6, SDA → PE7
- **GPIO 配置**: PE6/PE7 设为功能 IO，SDA 加上拉
- **IIC 配置**（参考手册 §8）:
  - 时钟源选择和预分频配置
  - 波特率 100KHz（标准模式）
  - 配置 IICCMDA（设备地址 + 控制字节）
  - 配置 IICDATA（发送数据）
  - 配置 IICON1（启动/停止/数据使能）
  - 写 KS 启动传输
- **测试方法**: 发送 START + 7位地址(0x50) + W + 数据(0xA5) + STOP
  - 逻辑分析仪接 PE6(SCL) 和 PE7(SDA) 观察 I2C 时序

### 文档
- `docs/i2c_test_report.md`

---

## 引脚资源规划总表

| 外设 | 引脚 | Port | 功能 | 备注 |
|------|------|------|------|------|
| UART0 TX | PB3 | B | printf 输出 | 保持不动 |
| GPIO OUT | PE4 | E | 输出测试 | Phase 1 |
| GPIO IN | PF0 | F | 输入测试 | Phase 1 |
| PWM0 | PB0 | B | Timer3 PWM | Phase 2 |
| PWM1 | PB1 | B | Timer3 PWM | Phase 2 |
| PWM2 | PB2 | B | Timer3 PWM | Phase 2 |
| UART1 TX | PA4 | A | 发送 | Phase 3 |
| UART1 RX | PA3 | A | 接收 | Phase 3 |
| SPI1 CLK | PE6 | E | 时钟 | Phase 4 |
| SPI1 MOSI | PE7 | E | 主机输出 | Phase 4 |
| SPI1 MISO | PE5 | E | 主机输入 | Phase 4 |
| I2C SCL | PE6 | E | 时钟 | Phase 5 |
| I2C SDA | PE7 | E | 数据 | Phase 5 |

---

## 待确认事项（实现时逐个确认）

1. **SPI1 FUNCMCON1[15:12] 组号映射**: 手册只写了 SPI0MAP 的 G1~G3 说明。SPI1MAP 推断 G1=1, G2=2, ..., G5=5, 0xF=清除。如有疑问参考引脚功能表交叉验证。
2. **IIC FUNCMCON2[27:24] 组号映射**: 同上推断 G1=1, ..., G8=8。
3. **I2C 时钟源配置**: 手册 §8.2 提到选择 RC2M 或 XOSC26M，但 IICON0 寄存器中没有显式的时钟源选择位，需要进一步查看是否有相关 CLKCON 或 CLKGAT 寄存器控制。
4. **Timer3 PWM 时钟选择**: `INCSEL` 位域选择递增时钟源。现有代码中 Timer2 用系统时钟（默认），但 `CLKCON0[24]` 配置了 tmr_inc=1MHz。Timer3 的 INCSEL=00 是否使用 tmr_inc 还是独立系统时钟分支，需验证。
5. **main.c 集成方式**: 每个测试通过修改 `main()` 调用不同的测试函数，或通过条件编译切换。建议每个 Phase 单独替换测试入口。

---

## 文档规范

每个测试文档 `docs/xxx_test_report.md` 应包含：
- **测试目的**: 验证什么功能
- **测试原理**: 涉及的寄存器、位域、配置计算公式
- **硬件连接**: 引脚接线图
- **测试步骤**: 分步骤操作说明
- **预期结果**: 万用表/逻辑分析仪预期读数
- **实测结果**: 实际观察到的现象
- **结论**: 通过/不通过
