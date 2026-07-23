# BT892X I2C HAL 重构记录

> **状态**：已完成，**未 commit+push**（按用户惯例：代码改完后等用户烧板验证，OK 再 commit）。
> **测试入口**：[smart_mini/test/test_i2c.c](../smart_mini/test/test_i2c.c) + 3 个变体（gpio / la / gpio_la）
> **新增 HAL**：[smart_mini/test/i2c_hal.h](../smart_mini/test/i2c_hal.h) + [i2c_hal.c](../smart_mini/test/i2c_hal.c)
> **相关参考**：仿 [SPI HAL 重构记录](test_spi_hal_refactor.md) + [UART HAL 重构记录](test_uart_hal_refactor.md) 模式

---

## 0. 背景与目标

完成 SPI / UART HAL 重构后，I2C 测试代码仍是 4 个 `test_i2c*.c` 文件各自实现：

- `test_i2c.c`（286 行）+ `test_i2c_gpio.c`（276 行）—— AT24C02 功能测试
- `test_i2c_la.c`（143 行）+ `test_i2c_gpio_la.c`（112 行）—— LA 时序测试

每份里都重复：
- 11 个 IICCON0/1 寄存器位宏（`IIC_EN` / `IIC_KS` / `IIC_START0_EN` 等）
- PE6/PE7 PAD 配置（GPIO + FEN + DIR + PU）
- FUNCMCON2 G5 映射
- IICCON0 / POSDIV / CLR_ALL
- 写寄存器流程（CMDA + DATA + CON1 action + KS + 等 DONE + 清 DONE）
- AT24C02 sub-address 1 字节地址读写

重构后 4 个 test 入口缩减到 50~80 行 / 个，重复寄存器代码全部进 HAL。

---

## 1. 最终架构

```
smart_mini/test/
├── i2c_hal.h          [新建] HAL 接口 + AT24C02 驱动声明
├── i2c_hal.c          [新建] HAL 实现 (软 5 + 硬 5 + 业务 12)
├── test_i2c.c         [重构] 硬件 AT24C02 测试 (95 行)
├── test_i2c_gpio.c    [重构] 软件 bit-bang AT24C02 测试 (95 行)
├── test_i2c_la.c      [重构] 硬件 LA 时序测试 (60 行)
└── test_i2c_gpio_la.c [重构] 软件 bit-bang LA 时序测试 (50 行)
```

---

## 2. i2c_hal 接口总览

3 层 API：底层原语 → 中层 transaction → 业务封装。

### 2.1 底层原语（bit-bang + 硬件各 5 个）

```c
// 软件 bit-bang 原语（PE6=SCL 输出，PE7=SDA DIR 切换模拟开漏）
void i2c_hal_soft_init(void);
void i2c_hal_soft_start(void);
void i2c_hal_soft_stop(void);
bool i2c_hal_soft_write_byte(u8 data);    // true=ACK
u8   i2c_hal_soft_read_byte(bool send_ack);  // send_ack=true=ACK, false=NAK

// 硬件 IIC 原语（PE6/PE7 G5 映射 → IIC 控制器）
void i2c_hal_hw_init(void);
bool i2c_hal_hw_wait_done(u32 timeout_us);
bool i2c_hal_hw_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us);
bool i2c_hal_hw_write(u8 dev_addr7, u8 reg_addr,
                      const u8 *data, u8 len, u32 timeout_us);
bool i2c_hal_hw_read(u8 dev_addr7, u8 reg_addr,
                     u8 *buf, u8 len, u32 timeout_us);
```

### 2.2 中层 transaction

```c
bool i2c_hal_soft_probe_addr(u8 dev_addr7, bool is_read);
bool i2c_hal_soft_write(u8 dev_addr7, u8 reg_addr,
                        const u8 *data, u8 len);
bool i2c_hal_soft_read(u8 dev_addr7, u8 reg_addr,
                       u8 *buf, u8 len);
```

### 2.3 业务封装（AT24C02，软/硬同名同形）

```c
// 硬件版
bool at24c02_hw_probe(u8 addr7);
u32  at24c02_hw_scan(u8 addr_lo, u8 addr_hi);
bool at24c02_hw_write_byte(u8 reg, u8 val);  // 内置 delay_ms(10) 等写周期
bool at24c02_hw_read_byte(u8 reg, u8 *out);
bool at24c02_hw_write_bytes(u8 reg, const u8 *buf, u8 len);
bool at24c02_hw_read_bytes(u8 reg, u8 *buf, u8 len);

// 软件版（同名同形）
bool at24c02_soft_probe(u8 addr7);
u32  at24c02_soft_scan(u8 addr_lo, u8 addr_hi);
bool at24c02_soft_write_byte(u8 reg, u8 val);
bool at24c02_soft_read_byte(u8 reg, u8 *out);
bool at24c02_soft_write_bytes(u8 reg, const u8 *buf, u8 len);
bool at24c02_soft_read_bytes(u8 reg, u8 *buf, u8 len);
```

---

## 3. 软/硬 HAL 实现关键差异

### 3.1 硬件 IIC（[i2c_hal.c L228-353](../smart_mini/test/i2c_hal.c)）

手册 §8.3 步骤：
1. 开 `CLKGAT2[0]`（IIC 时钟门）
2. PE6/7 PAD: digital + pull-up + `FEN=1`（硬件接管）
3. `FUNCMCON2[27:24] = 0x5`（G5：PE6=SCL, PE7=SDA）
4. `IICCON0 = (POSDIV=19<<4) | IIC_EN` + `CLR_ALL`
5. 装 `IICCMDA` (CTL0 + ADR0/1) + `IICDATA` (1-4 字节 packed)
6. 装 `IICCON1` action sequence
7. `IICCON0 |= KS` kick start
8. 等 `IICCON0[DONE]`（带 timeout）
9. 读 `ACKSTATUS` + `IICCON0 |= CLR_DONE`

**关键寄存器位**（[i2c_hal.h L42-58](../smart_mini/test/i2c_hal.h)）：

```c
#define I2C_HW_EN        BIT(0)   // IICCON0[0]
#define I2C_HW_CLR_ALL   BIT(27)  // IICCON0[27]
#define I2C_HW_KS        BIT(28)  // IICCON0[28]
#define I2C_HW_CLR_DONE  BIT(29)  // IICCON0[29]
#define I2C_HW_ACKSTATUS BIT(30)  // IICCON0[30] - 0=ACK, 1=NAK
#define I2C_HW_DONE      BIT(31)  // IICCON0[31]
#define I2C_HW_START0_EN BIT(3)   // IICCON1[3]
#define I2C_HW_CTL0_EN   BIT(4)   // IICCON1[4]
// ... 见头文件
```

### 3.2 软件 bit-bang（[i2c_hal.c L43-148](../smart_mini/test/i2c_hal.c)）

- PE6 = SCL（一直输出）
- PE7 = SDA（开漏：`DIR=0` 输出 / `DIR=1` 输入释放，靠上拉拉高）
- 时序（Standard mode 100 kHz）：
  - SDA 切换 → `delay_us(1)` data setup
  - SCL HIGH → `delay_us(5)` → 主机/从机采样 SDA
  - SCL LOW → `delay_us(5)`

关键宏（在 i2c_hal.c 内部）：

```c
#define I2C_SCL_OUT()      do { GPIOEDIR &= ~I2C_SCL_PIN; } while (0)
#define I2C_SDA_IN()       do { GPIOEDIR |=  I2C_SDA_PIN; } while (0)
#define I2C_DELAY()        delay_us(5)
#define I2C_TSU()          delay_us(1)
```

### 3.3 业务封装设计

`at24c02_hw_*` 和 `at24c02_soft_*` 是软/硬**对称**的 API —— 测试代码切换 hardware ↔ software 只需换前缀：
- `i2c_hal_hw_init()` ↔ `i2c_hal_soft_init()`
- `at24c02_hw_write_byte()` ↔ `at24c02_soft_write_byte()`

这种 template-style 复用正是 SPI/UART HAL 一直以来的设计哲学。

---

## 4. 4 个测试文件重构前后对比

### 4.1 test_i2c.c（硬件 AT24C02 测试）

| 阶段 | 行数 | 寄存器代码 |
|---|---|---|
| 重构前 | 286 行 | 11 个 IIC 位宏 + `test_i2c_init` + `test_iic_wait_done` + `test_iic_probe_addr` + `test_iic_write` + `test_iic_read`（共 ~150 行裸代码）|
| 重构后 | 95 行 | 0 行；只调 HAL + at24c02_hw_* API |

**典型对比**（Test 3 — write single byte）：

```c
// 重构前
u8 val = 0x55;
bool ok = test_iic_write(AT24C02_ADDR, 0x00, &val, 1, 100000);
TEST_LOG("  Write: %s", ok ? "ACK" : "NAK/TIMEOUT");
if (ok) delay_ms(10);

// 重构后
u8 val = 0x55;
bool ok = at24c02_hw_write_byte(0x00, val);
printf("  Write: %s\n", ok ? "ACK" : "NAK/TIMEOUT");
```

### 4.2 test_i2c_gpio.c（软件 bit-bang AT24C02 测试）

| 阶段 | 行数 | 寄存器代码 |
|---|---|---|
| 重构前 | 276 行 | SCL/SDA 宏 6 个 + start/stop/write_byte/read_byte + probe_addr/write/read + init_pads |
| 重构后 | 95 行 | 0 行；只调 HAL |

**典型对比**（Test 5 — 4-byte pattern）：

```c
// 重构前
ok = i2c_gpio_write(AT24C02_ADDR, 0x10, wr_buf, 4);
TEST_LOG("  Write 0xDE 0xAD 0xBE 0xEF: %s", ok ? "ACK" : "NAK");
if (ok) delay_ms(10);
ok = i2c_gpio_read(AT24C02_ADDR, 0x10, rd_buf, 4);
TEST_LOG("  Read: %s, data=0x%02x 0x%02x 0x%02x 0x%02x", ...);

// 重构后
ok = at24c02_soft_write_bytes(0x10, wr_buf, 4);
printf("  Write 0xDE 0xAD 0xBE 0xEF: %s\n", ok ? "ACK" : "NAK");
ok = at24c02_soft_read_bytes(0x10, rd_buf, 4);
printf("  Read: %s, data=0x%02x 0x%02x 0x%02x 0x%02x\n", ...);
```

### 4.3 test_i2c_la.c / test_i2c_gpio_la.c（LA 时序测试）

| 文件 | 重构前 | 重构后 |
|---|---|---|
| test_i2c_la.c | 143 行（含 `la_iic_init` 22 行 + `la_probe` 27 行 + IIC 位宏 8 个）| 60 行（burst 调用 `i2c_hal_hw_probe_addr`）|
| test_i2c_gpio_la.c | 112 行（含 SCL/SDA 宏 + `la_init_pads` 10 行 + `la_burst` 36 行）| 50 行（burst 调用 `i2c_hal_soft_*`）|

**关键简化**：LA 测试原本要把 `START+addr+STOP` 序列展开成 GPIO 操作（35 行）；HAL 后变成 3 行：

```c
// 重构后
i2c_hal_soft_start();
(void)i2c_hal_soft_write_byte((u8)((AT24C02_ADDR << 1) | 0u));
i2c_hal_soft_stop();
```

---

## 5. TEST_LOG → printf 切换

按用户要求，把所有 `TEST_LOG(...)` 改为 `printf(...)`。

**关键点**：`TEST_LOG(fmt, ...)` 宏定义为：
```c
#define TEST_LOG(fmt, ...)  printf("[TEST] " fmt "\n", ##__VA_ARGS__)
```

替换时需注意：
1. 函数名替换：`TEST_LOG(` → `printf(`
2. **补 `\n`**：`TEST_LOG` 末尾有 `\n`，`printf` 没有——需要在每个 `printf("...")` 的字符串末尾补 `\n`

替换方式：手工逐文件重写（避免正则替换两次插入重复 `\n`），4 个 test 文件共 99 处 `TEST_LOG` 全部转换。

---

## 6. 总行数变化

| 文件 | 重构前 | 重构后 | Δ |
|---|---|---|---|
| test_i2c.c | 286 | 95 | **-191 (-67%)** |
| test_i2c_gpio.c | 276 | 95 | **-181 (-66%)** |
| test_i2c_la.c | 143 | 60 | **-83 (-58%)** |
| test_i2c_gpio_la.c | 112 | 50 | **-62 (-55%)** |
| i2c_hal.c | 0 | 353 | **+353 (新增)** |
| i2c_hal.h | 0 | 87 | **+87 (新增)** |
| **小计** | **817** | **740** | **-77 (-9%)** |

虽然总行数减少不多（HAL 加了 440 行，4 个 test 减了 517 行，净 -77），但**重复寄存器代码从 4 份变成 1 份**——后续改 IIC 时序或换芯片只需要改 `i2c_hal.c` 一个文件。

---

## 7. 验证清单

- [x] 19 个 .c 编译 `-Os -Wall -Wextra -ffunction-sections -fdata-sections` 0 warning
- [x] `--gc-sections` 链接 OK
- [x] postbuild → "CODE SIZE: 12 KB" / app.dcf 12 KB
- [x] map.txt 含 `i2c_hal_soft_init/start/stop/write_byte/read_byte/probe_addr/write/read` + `i2c_hal_hw_init/wait_done/probe_addr/write/read` + `at24c02_*`
- [ ] **烧板验证**（用户执行，未跑）：
  - [ ] `TEST_I2C_EN`：probe ACK / scan 1 个设备 (0x50) / write 0x55 ACK / read=0x55 / 4-byte 0xDEADBEEF PASSED
  - [ ] `TEST_I2C_GPIO_EN`：同上 5 个
  - [ ] `TEST_I2C_LA_EN`：60 × 2 burst，初始寄存器 dump
  - [ ] `TEST_I2C_GPIO_LA_EN`：60 × 2 burst，LA 抓 SCL ~12µs = 83 kHz

---

## 8. 工程意义

1. **代码去重**：4 个测试文件的 ~300 行裸代码变成 5 行 HAL 调用——下次调 IIC 时序 / 加 AT24C32 / 换从设备只改 `i2c_hal.c` 一处
2. **可读性提升**：测试代码现在只看得出"测什么场景"，不掺寄存器细节
3. **教学价值**：`i2c_hal.h` 是 BT892X IIC 操作的完整参考实现（手册 §8.3 step 1-9 一一对应）
4. **与 SPI/UART HAL 对齐**：3 个外设（SPI / UART / I2C）的 HAL 都是相同的设计模式——`xxx_hal_{soft,hw}_init/byte` + `device_xxx_{soft,hw}_*`

---

## 9. 相关文档

- [docs/periph_i2c.md](periph_i2c.md)（IIC 寄存器/引脚/初始化原理）
- [docs/test_hard_i2c.md](test_hard_i2c.md)（硬件 IIC 原始测试报告）
- [docs/test_soft_i2c.md](test_soft_i2c.md)（软件 IIC 原始测试报告）
- [docs/test_spi_hal_refactor.md](test_spi_hal_refactor.md)（SPI HAL 重构参考）
- [docs/test_uart_hal_refactor.md](test_uart_hal_refactor.md)（UART HAL 重构参考）
- [BT892X_UserManual_Driver.md §8](../BT892X_UserManual_Driver.md)（IIC 寄存器 + §8.3 步骤）
- [bt892x_pinfunction.md §4.3](../bt892x_pinfunction.md)（PE6/PE7 G5 映射）

---

## 10. 版本

- 2026-07-23 v1：HAL 重构 + TEST_LOG → printf，单 commit 待用户烧板验证后另起 commit