# BT892X SPI 测试报告

> **测试日期**：2026-07-17
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §7 SPI](../BT892X_UserManual_Driver.md)（SPICON/BUF/BAUD/CPND/DMA）
> - [BT892X_UserManual_Driver.md §3.3 FUNCMCON1](../BT892X_UserManual_Driver.md)（SPI1MAP）
> - [BT892X_UserManual_Driver.md §3.2 GPIO](../BT892X_UserManual_Driver.md)（GPIO 寄存器）
> - [bt892x_pinfunction.md §4.3 PORTE / §5.2 SPI1](../bt892x_pinfunction.md)
> **相关 commit**：`git log smart_mini_minimax` 查看
> **测试模块**：**软/硬 SPI 回环 + 逻辑分析仪波形 + W25Q64 Flash + 汇编优化 + polling/INT/DMA 时间对比**
> **测试结果**：✅ Phase 1 回环（软+硬 256/256）✅ Phase 2 波形（软+硬 0x55 LA 解码正确）⬜ Phase 3 W25Q64 + ASM + 时间对比（已就绪，待后续测试）

---

## 0. 文档结构

按接线互斥分 5 个测试入口（每次烧一个）：
- **跳线接法**（PE7↔PE5 跳线）→ soft + HW SPI 回环
- **LA 接法**（CH0=PE6/CH1=PE7）→ 持续波形
- **W25Q64 Flash 接法**（CS=PE4/CLK=PE6/DI=PE7/DO=PE5）→ 软件 + 硬件 W25Q64 全套实验
- **纯 GPIO** → 软件 bit-bang C/ASM/ASM-展开 速度对比
- **Flash 接法（同上）** → polling/INT/DMA 时间对比

| 程序 | 路径 | 入口/开关 | 用途 |
|---|---|---|---|
| **test_spi_loop.c** | `smart_mini/test/test_spi_loop.c` | `test_spi_loop_run` / `TEST_SPI_LOOP_EN` | 软件 SPI 回环 + 硬件 SPI1 回环（跳线 PE7↔PE5） |
| **test_spi_wave.c** | `smart_mini/test/test_spi_wave.c` | `test_spi_wave_run` / `TEST_SPI_WAVE_EN` | 软件/硬件 SPI 持续发 0x55（LA 观察） |
| **test_spi_w25q64.c** | `smart_mini/test/test_spi_w25q64.c` | `test_spi_w25q64_run` / `TEST_SPI_W25Q64_EN` | 软/硬 W25Q64 全套实验（MODE=0/1/2 + EXP 单选） |
| **test_spi_soft_asm.c** | `smart_mini/test/test_spi_soft_asm.c` | `test_spi_soft_asm_run` / `TEST_SPI_SOFT_ASM_EN` | 软件 bit-bang C vs ASM vs ASM-unrolled 速度对比 |
| **test_spi_timing.c** | `smart_mini/test/test_spi_timing.c` | `test_spi_timing_run` / `TEST_SPI_TIMING_EN` | polling/INT/DMA 时间对比（100KHz + 12MHz） |

> **接法互斥**：跳线 / LA / W25Q64 Flash 三者**同一时刻只能选一种**，因为都用 PE4~PE7。共享引脚（§1.5）：
> | 引脚 | 软 SPI 角色 | 硬 SPI 角色 | Flash 用法 |
> |---|---|---|---|
> | PE4 | CS(GPIO) | CS(GPIO) | CS |
> | PE6 | CLK(GPIO) | CLK(SPI1) | CLK→SCK |
> | PE7 | MOSI/DI(GPIO) | MOSI/SPI1DO | DI(Flash) |
> | PE5 | MISO/DO(GPIO) | MISO/SPI1DI | DO(Flash) |

---

## 1. 手册源码引用

### 1.1 SPI 寄存器（手册 [§7.2](../BT892X_UserManual_Driver.md) 第 466-513 行；SPI0/SPI1 寄存器结构一致）

```
SPIxCON 位
Bit  | Name    | Mode | Description
-----|---------|------|--------------------------------------------------
 16  | SPIPND  | R    | SPI 挂起。0:未完成；1:收发完成（手册 §7.2 第 470 行）
 10  | SPIOSS  | WR   | 采样边沿。0:与输出数据不同；1:同边沿（手册第 474 行）
 9   | SPIMBEN | WR   | 多位总线使能（本测试不用，第 475 行）
 8   | SPILF_EN| WR   | LFSR 使能（本测试不用）
 7   | SPIIE   | WR   | SPI 中断使能。0:禁用；1:使能（第 477 行）
 6   | SMPS    | WR   | 输出边沿（SPIOSS=0 时）。0:下降沿输出；1:上升沿输出
 5   | CLKIDS  | WR   | 空闲时钟状态。0:低电平；1:高电平 → 即 CPOL
 4   | RXSEL   | WR   | DMA 方向。0:发送；1:接收（第 480 行）
 3:2 | BUSMODE | WR   | 数据总线宽度。00:3线；01:2线；10:2位双向；11:4位双向
 1   | SPISM   | WR   | 主机/从机。0:主机；1:从机
 0   | SPIEN   | WR   | SPI 使能。0:禁用；1:使能（第 483 行）

SPIxBAUD[15:0] : 波特率 = Fsys / (SPI_BAUD + 1)   （手册第 489 行）
SPIxCPND[16]   : 写 1 清除 SPI 挂起              （手册第 495 行 SPICPND）
SPIxBUF[7:0]   : 写加载发送、读获取接收            （手册第 501 行）
SPIxDMACNT[10:0]: DMA 字节数，写此寄存器启动 DMA    （手册第 507 行）
SPIxDMAADR[20:0]: DMA 字节地址                    （手册第 512 行）
```

**Mode 0 配置本测试所用位**：SPISM=0（主机）、BUSMODE=00（3线）、CLKIDS=0（CPOL=0空闲低）、SMPS=0（下降沿输出）、SPIOSS=0（采样数据不同边沿）、SPIEN=1。本测试代码里把这 6 个位写成 `SPI1CON = BIT(0)`（Mode 0 默认其他位为 0，所以只写 SPIEN）。

> **注意**：手册命名 `SPIxCON` 中的 x=0，本测试用 SPI1（sfr.h 第 579 行 `SPI1CON`）。手册第 7 节明确表示 SPI0/SPI1 共享相同寄存器结构，故位定义对 SPI1 同样适用。

### 1.2 SPI 使用流程（手册 [§7.3](../BT892X_UserManual_Driver.md) 第 515-526 行）

```
1. 设 IO 方向
2. 选择 RXSEL
3. 配时钟频率 (= Fsys / (BAUD+1))
4. 选 4 种时序之一
5. 置位 SPIEN
6. 选配 SPIIE
7. 写 SPIBUF 启动传输
8. 等 SPIPND=1 或中断
9. 读 SPIBUF 拿接收
```

### 1.3 SPI1 引脚映射（手册 [§3.3 FUNCMCON1](../BT892X_UserManual_Driver.md) 第 172-177 行）

```
FUNCMCON1[15:12] SPI1MAP : 0001=G1, 0010=G2, 0011=G3, 0100=G4, 0101=G5, 0110=G6, 0111=G7, 1111=清除
```

**本测试用 G4** = PE6(CLK), PE7(DO), PE5(DI)：对照 [pinfunction §4.3 第 127-129 行](../bt892x_pinfunction.md)，G4 组如下：

| 引脚 | 角色（G4） | 手册依据 |
|---|---|---|
| **PE6** | SPI1CLK | §4.3 第 128 行 `SPI1CLK-G4` |
| **PE7** | SPI1DO/SPI1DATA | §4.3 第 129 行 `SPI1DO/SPI1DATA-G4` |
| **PE5** | SPI1DI | §4.3 第 127 行 `SPI1DI-G4` |

> CS 不用映射，**手动用 GPIO 控制**（PE4）。

### 1.4 时钟源与延迟单位

- 系统时钟 = `set_sys_clk(SYS_24M)` = **24 MHz**（main.c 第 198 行）
- `SPI1BAUD = 239` → 24MHz / 240 = **100 kHz**（适合 LA 观察）
- `SPI1BAUD = 1` → 24MHz / 2 = **12 MHz**（测试 DMA 大数吞吐）
- 软件 SPI bit-bang 用 `delay_us(N)` 按时序，依赖 `tmr_inc = 1MHz`（x26m_div_clk），1 µs/tick（main.c 第 191-193 行 + [test_timer.md §1.1](test_timer.md)）

### 1.5 GPIO（手册 [§3.2](../BT892X_UserManual_Driver.md) 第 141-157 行，详细位表见 [test_gpio.md §1](test_gpio.md)）

本测试软件 SPI 操作 PE4/PE5/PE6/PE7 用到的寄存器：
- `GPIOEDE`（数字使能）、`GPIOEFEN`（功能映射，硬件 SPI 时设为 1、软件 SPI 时清 0）
- `GPIOEDIR`（方向：CS/CLK/MOSI=0 输出，MISO=1 输入）
- `GPIOESET`（写 1 置 1）、`GPIOECLR`（写 1 清 0）、`GPIOE`（读 MISO 采样）

---

## 2. Phase 1：test_spi_loop.c（跳线回环，软+硬）

### 2.1 引脚分配

| 引脚 | 软件 SPI 角色 | 硬件 SPI1 G4 角色 | 接法 |
|---|---|---|---|
| PE4 | CS(GPIO 输出) | CS(GPIO 输出) | 同上 |
| PE6 | CLK(GPIO 输出) | CLK(SPI1) | 同上 |
| PE7 | MOSI/数据(GPIO 输出) | DO(MOSI/SPI1) | **跳线短接到 PE5** |
| PE5 | MISO/数据(GPIO 输入) | DI(SPI1) | **PE7→PE5**（同跳线另一端） |

### 2.2 寄存器配置原理

**软件 SPI bit-bang**（纯 GPIO，无 SPI 控制器）：
```c
GPIOEFEN &= ~(CS|CLK|MOSI|MISO);    // §3.2: FEN=0 用作 GPIO
GPIOEDE  |=  (CS|CLK|MOSI|MISO);    // §3.2: DE=1 数字 IO
GPIOEDIR &= ~(CS|CLK|MOSI);         // §3.2: CS/CLK/MOSI 输出
GPIOEDIR |=  MISO;                  // §3.2: MISO 输入
GPIOESET  =  CS;                    // §3.2: CS 空闲高
GPIOECLR  =  CLK;                   // §3.2: CLK 空闲低（Mode 0）
```

**软件 bit 发送**（MSB-first，每位 ~3µs）：
```c
for (int i = 7; i >= 0; i--) {
    if (tx & (1 << i)) GPIOESET = MOSI; else GPIOECLR = MOSI;   // §3.2: 先置 MOSI
    delay_us(1);
    GPIOESET = CLK; delay_us(1);                                 // §3.2: CLK 上升沿（采样）
    if (GPIOE & MISO) rx |= (1 << i);                            // §3.2: 中心采样 MISO
    GPIOECLR = CLK; delay_us(1);                                 // §3.2: CLK 下降沿
}
```

**硬件 SPI1 初始化**：
```c
FUNCMCON1 &= ~(0xF << 12);
FUNCMCON1 |=  (0x4 << 12);    // §3.3 SPI1MAP = G4
GPIOEFEN |= CLK|MOSI|MISO; GPIOEDE|= same; GPIOEDIR &= ~(CLK|MOSI); GPIOEDIR |= MISO;
GPIOEFEN &= ~CS; GPIOEDE|= CS; GPIOEDIR &= ~CS; GPIOESET = CS;   // §3.2 CS 用 GPIO
SPI1BAUD = 239;                // §7.2: 100 kHz
SPI1CON  = BIT(0);             // §7.2: SPIEN=1, Mode 0 其他位默认 0
```

**硬件 SPI 收发一字节**（`hw_spi_byte`）：
```c
SPI1BUF = tx;
while (!(SPI1CON & BIT(16)));     // §7.2: 等 SPIPND=1（收发完成）
SPI1CPND = BIT(16);               // §7.2: 写 1 清挂起
return (u8)SPI1BUF;               // §7.2: 读接收
```

### 2.3 测试流程

| 子测试 | 验证 | 期望 |
|---|---|---|
| Phase 1.1 软件回环 | bit-bang 0x00~0xFF，PE7→PE5 跳线 | `Soft Loopback: PASSED (0/256 errors)` |
| Phase 1.2 硬件回环 | SPI1 G4 0x00~0xFF，PE7→PE5 跳线 | `HW Loopback: PASSED (0/256 errors)` |

### 2.4 实测结果（用户验证）

```
===== BT892X SPI Loopback Test (Software + Hardware) =====
Wiring: jumper PE7 <-> PE5

##### Phase 1: Software SPI Loopback (bit-bang) #####
Loopback: PASSED (0/256 errors)

##### Phase 2: Hardware SPI1 Loopback #####
HW Loopback: PASSED (0/256 errors)
```

**结论**：✅ 软件 bit-bang 和硬件 SPI1 都通过 256/256 回环，证明收发通路、引脚映射、时序都对。

---

## 3. Phase 2：test_spi_wave.c（逻辑分析仪波形，软+硬）

### 3.1 引脚与接法

| 引脚 | 角色 |
|---|---|
| PE4 | CS（手动拉低/拉高包络每个字节） |
| PE6 | CLK（SCLK，**LA CH0**） |
| PE7 | MOSI（**LA CH1**） |
| PE5 | MISO（**不接 LA、也不用**） |

**LA 配置**：
- CH0 = SCLK（捕获 PE6）— CPOL=0 空闲低
- CH1 = MOSI（捕获 PE7）— MSB-first
- CPHA=0（数据在前一个时钟沿有效，即**上升沿有效**）
- 8 位 / 字，无校验，CS=空（**字节边界靠间距确认**）
- ⚠️ **采样率推荐 ≥ 10× 信号频率**（软件 SPI 每位 1µs → ≥ 10 MS/s；硬件 SPI @ 100kHz 每位 ~10µs → ≥ 1 MS/s）

### 3.2 寄存器配置

**软 SPI**：同 §2.2（PE4/PE6/PE7 输出，PE5 输入），持续发 `0x55`。
**硬 SPI**：同 §2.2 硬件 init (`SPI1BAUD=239, SPI1CON=BIT(0)`)，持续发 `0x55`。

每次 1 字节：
```c
GPIOECLR = CS; delay_us(1);  // CS LOW 包络
soft_spi_byte(0x55) / hw_spi_byte(0x55);
GPIOESET = CS; delay_us(1);  // CS HIGH
delay_us(100);               // 字节间间隔供 LA 帧对齐
```

### 3.3 实测结果（用户验证）

- ✅ **软 SPI（PHASE=1）**：PE5/PE6/PE7 输出持续 0x55 序列，LA 抓到 8-clock + 01010101 波形
- ✅ **硬 SPI（PHASE=2）**：PE6(CLK)/PE7(MOSI) 输出持续 0x55，**LA 解码每个字节都是 `0x55`**，证明：
  - SPI1 G4 映射（§3.3）配置正确
  - CPOL=0/CPHA=0（手册 §7.2 SPI0CON CLKIDS/SMPS/SPIOSS）符合实际波形
  - LA 采样率足够高（≥10×信号频率）

> **本次测试顺手 fix 的 bug**：测试初版把软件 SPI 的 CLK/MOSI 仍按 copilot 旧引脚（PE5=CLK, PE6=MOSI），与硬件 SPI 的 PE6=CLK/PE7=MOSI 不一致。修齐后，**软/硬 SPI 共用 PE4/PE6/PE7（CS/CLK/MOSI）+ PE5（MISO）**，**软硬件波形测试两阶段 LA 探头接线无需重接**。

---

## 4. Phase 3：test_spi_w25q64.c（软/硬 W25Q64 全套实验） ⬜ 待测

代码已就绪。运行时配置（构建选项）：
- `-DSPI_W25_RUN_MODE=2` 一键跑完软+硬
- `-DSPI_W25_RUN_MODE=0`（默认）只软，`=1` 只硬
- 软件/硬件实验分别由 `SPI_SW_W25_MODE` 和 `SPI_HW_W25_MODE` 控制 0=单 exp（SPI_*_W25_EXP=1） / 1=全测（含 Chip Erase，会清空 Flash！） / 2=仅读
- 实验：JEDEC ID、Status、Page Write/Read、Cross-Page、Sector Erase、Erase Timing、Write Protect、Unique ID/SFDP

**待用户后续测试 + 文档补充**。

---

## 5. Phase 4：test_spi_soft_asm.c（软件 bit-bang C vs ASM 速度优化） ⬜ 待测

代码已就绪。接法：纯 GPIO，无需外围。运行后打印三版本耗时（C / ASM循环 / ASM展开），并用 JEDEC ID 验证三者功能正确性。

**待用户后续测试 + 文档补充**。

---

## 6. Phase 5：test_spi_timing.c（polling/INT/DMA 时间对比） ⬜ 待测

代码已就绪。接法：需要 W25Q64 Flash（同 §4）。运行：
- 中断模式：JEDEC ID + 512B/4096B 中断读取耗时
- DMA 模式：100 kHz/12 MHz 下 Polling vs DMA 耗时对比，DMA page program 256B 校验

**待用户后续测试 + 文档补充**。

---

## 7. 完整寄存器表

| 寄存器 | 地址（sfr.h） | 本测试配置 | 手册依据 |
|---|---|---|---|
| `SPI1CON` | 0x980 (`SFR9_BASE+0x20*4`) | `BIT(0)` SPIEN（Mode 0）+ `BIT(7)` SPIIE（中断模式） | §7.2 |
| `SPI1BUF` | 0x984 (`SFR9_BASE+0x21*4`) | 读收/写发 | §7.2 第 501 行 |
| `SPI1BAUD` | 0x988 (`SFR9_BASE+0x22*4`) | `239`=100kHz / `1`=12MHz | §7.2 第 489 行 |
| `SPI1CPND` | 0x98C (`SFR9_BASE+0x23*4`) | `= BIT(16)` 清挂起 | §7.2 第 495 行 |
| `SPI1DMACNT` | 0x990 (`SFR9_BASE+0x24*4`) | 待测（DMA mode 阶段） | §7.2 第 507 行 |
| `SPI1DMAADR` | 0x994 (`SFR9_BASE+0x25*4`) | 待测（DMA mode 阶段） | §7.2 第 512 行 |
| `FUNCMCON1` | 0x020 (`SFR0_BASE+0x08*4`) | `[15:12]=4` (SPI1MAP G4) | §3.3 |
| `GPIOEDE` / `GPIOEFEN` | 0x690 / 0x694 | 软=0/0；硬=1/1 | §3.2 |
| `GPIOEDIR` | 0x68C | CS/CLK/MOSI=0（输出），MISO=1 | §3.2 |
| `GPIOESET` / `GPIOECLR` / `GPIOE` | 0x680 / 0x684 / 0x688 | 软 SPI 收发；硬 SPI CS 控制 | §3.2 |
| `TMR2CNT` | 0x0F0 (`SFR0_BASE+0x3C*4`) | `tick_get()` 给时序测试提供 1µs tick | main.c timer2_init |

---

## 8. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| 回环 256/256 全 0x00 | MOSI/MISO 跳线反了或未接 | 确认 PE7↔PE5 |
| HW 回环错位 1 bit | SPI Mode 配置错（CPOL/CPHA） | 默认 Mode 0；如确认是 Mode 3 改 `SPI1CON = BIT(0)\|BIT(5)` |
| LA 解码不规律 | 没接 CS 通道导致字节边界错位 | LA 加探针到 PE4/CS 极性低有效 |
| LA 完全无数据 | 采样率太低 | 软 SPI ≥10 MS/s，硬 SPI@100kHz ≥1 MS/s |
| LA 解码非 0x55 但有正常 8-bit 模式 | 探头接错/接错引脚 | 确认 CH0=PE6、CH1=PE7 |
| 软 SPI bit-by-bit 错误 | delay_us(1) 时钟太短 | 改 delay_us(5) 或更大 |
| 硬件 SPI 返回 0xFF | CS 一直拉高 | 拉低 PE4 |
| 硬件 SPI 卡在等待 | SPIPND 未清 | `SPI1CPND = BIT(16)` |
| W25Q64 JEDEC 读 0xFF | CS 没接上 flash | 检查 CS=PE4 接对 |
| W25Q64 Chip Erase 等很久 | W25Q64 正常 ~20s（手册规定） | 等待即可 |
| DMA 返回全 0 | `RXSEL` 错方向 | DMA 接收 = `SPI1CON \|= BIT(4)`，发送 = `&= ~BIT(4)` |
| DMA 中断不触发 | SPIIE 未开 | `SPI1CON \|= BIT(7)` + `PICEN \|= BIT(IRQ_SPI_VECTOR)` |

---

## 9. 工程意义

- **5 个测试入口覆盖 SPI 全维度**：跳线回环验证接口、LA 波形验证时序、W25Q64 验证 Flash 命令全集、ASM 验证编译器优化效果、timing 验证中断/DMA 性能。
- **引脚对齐**：软件 SPI bit-bang 与硬件 SPI1 G4 都使用 PE4=CS/PE6=CLK/PE7=MOSI/PE5=MISO，**一次接线同时测**（注意 W25Q64 测时 PE5 是输出 DI 到 flash，不能跳线）。
- **手册依据**：每条寄存器操作对应 §7.2/§3.3/§3.2 手册位表；每个引脚对应 pinfunction.md §4.3 PE 端口表。
- **完成度**：Phase 1（LOOP）+ Phase 2（WAVE）✅，Phase 3-5 ⬜ 待 W25Q64/时间对比/汇编 实测。

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_spi_common.h` | SPI 共用引脚宏定义（CS=PE4/CLK=PE6/MOSI=PE7/MISO=PE5） |
| `smart_mini/test/test_spi_loop.c/.h` | Phase 1：跳线回环（软+硬） |
| `smart_mini/test/test_spi_wave.c/.h` | Phase 2：LA 波形（软+硬） |
| `smart_mini/test/test_spi_w25q64.c/.h` | Phase 3：W25Q64 软/硬全套 |
| `smart_mini/test/test_spi_soft_asm.c/.h` | Phase 4：软件 bit-bang C vs ASM 速度对比 |
| `smart_mini/test/test_spi_timing.c/.h` | Phase 5：polling/INT/DMA 时间对比 |
| `smart_mini/main.c` | 5 个 SPI 开关 + dispatch（第 55-63 行的 SPI 段） |
| `smart_mini/app.cbp` | CodeBlocks 工程（注册 5 个 .c + 共用头） |
| `smart_mini/header/sfr.h` | SPI1 寄存器宏定义第 579-584 行、FUNCMCON1 第 45 行、GPIOE 第 450-462 行 |
| `docs/BT892X_UserManual_Driver.md` | 手册 §7 SPI（第 455-541 行）、§3.3 FUNCMCON1（第 172-177 行）、§3.2 GPIO（第 134-157 行） |
| `docs/bt892x_pinfunction.md` | 引脚 §4.3 PORTE（第 121-129 行）、§5.2 SPI1（第 211-212 行） |
